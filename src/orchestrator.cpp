#include "saci/orchestrator.hpp"

#include <unistd.h>

#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>

#include "saci/proc.hpp"
#include "saci/muxer.hpp"
#include "saci/stream.hpp"
#include "saci/subtitle.hpp"
#include "saci/sync.hpp"
#include "saci/translate.hpp"
#include "saci/tts.hpp"

namespace saci {

namespace {

// Traduz um lote com qualquer engine que satisfaca o conceito: usa
// translate_batch quando existe (argos lote, qwen) e cai no por-segmento
// caso contrario. if constexpr em acao — sem custo de runtime.
template <typename E>
std::vector<std::string> translate_all(E& eng,
                                       const std::vector<std::string>& texts) {
    if constexpr (requires { eng.translate_batch(texts); }) {
        return eng.translate_batch(texts);
    } else {
        std::vector<std::string> out;
        out.reserve(texts.size());
        for (const auto& t : texts) out.push_back(eng.translate(t));
        return out;
    }
}

} // namespace

const char* stage_name(Stage s) {
    switch (s) {
    case Stage::Idle: return "Idle";
    case Stage::FetchSubs: return "FetchSubs";
    case Stage::Parse: return "Parse";
    case Stage::Translate: return "Translate";
    case Stage::Tts: return "Tts";
    case Stage::SyncFit: return "SyncFit";
    case Stage::Render: return "Render";
    case Stage::StreamMux: return "StreamMux";
    case Stage::Done: return "Done";
    case Stage::Error: return "Error";
    }
    return "?";
}

void Orchestrator::enter(Stage s) {
    stage_ = s;
    std::cerr << "[saci] etapa: " << stage_name(s) << "\n";
}

int Orchestrator::run(const std::string& url,
                      const std::filesystem::path& workdir) {
    using namespace std::filesystem;
    create_directories(workdir);

    // v0.2: politicas por-site — o config efetivo mistura o global com a
    // primeira regra de site que casa com a URL (ver Config::effective_for).
    const Config eff = cfg_.effective_for(url);

    // 1. Legendas ------------------------------------------------------
    enter(Stage::FetchSubs);
    std::vector<std::string> dl = {"yt-dlp",
        "--write-subs", "--write-auto-subs",
        "--sub-langs", eff.sub_lang_pref,
        "--skip-download", "-P", workdir.string(), url};
    int code = run_quiet(dl);
    if (code != 0) { enter(Stage::Error); return code; }

    std::optional<path> srt;
    for (auto& e : directory_iterator(workdir))
        if (e.path().extension() == ".srt") { srt = e.path(); break; }
    if (!srt) { enter(Stage::Error); return 2; }

    // 2. Parse ---------------------------------------------------------
    enter(Stage::Parse);
    std::ifstream f(*srt);
    std::stringstream ss;
    ss << f.rdbuf();
    auto segments = parse_srt(ss.str());
    if (segments.empty()) { enter(Stage::Error); return 3; }

    // 3. Traducao (lote, uma unica invocacao quando o CLI colabora) -----
    enter(Stage::Translate);
    ArgosEngine tr(eff.source_lang, eff.target_lang, eff.translate_cmd);
    std::vector<std::string> texts;
    texts.reserve(segments.size());
    for (auto& s : segments) texts.push_back(s.text);

    std::vector<std::string> pt;
    if (eff.translate_backend == "qwen") {
        // v0.3: LLM open source chines (Qwen/Apache 2.0) via Ollama local
        QwenEngine q(eff.llm_cmd, eff.llm_model, eff.source_lang, eff.target_lang);
        pt = translate_all(q, texts);
    } else {
        ArgosEngine tr(eff.source_lang, eff.target_lang, eff.translate_cmd);
        pt = translate_all(tr, texts);
    }

    // 4. TTS -----------------------------------------------------------
    enter(Stage::Tts);
    PiperEngine piper(eff.tts_bin, eff.tts_voice);
    std::vector<Utterance> utts;
    utts.reserve(segments.size());
    for (std::size_t i = 0; i < segments.size(); ++i)
        utts.push_back(piper.synthesize(i, pt[i], workdir / "utt"));

    // 5. Plano de sincronia --------------------------------------------
    enter(Stage::SyncFit);
    SyncFitter fitter(SyncPolicy{eff.tempo_min, eff.tempo_max,
                                 eff.drift_threshold_ms});
    SyncFitter::DriftCorrector drift(eff.drift_threshold_ms);
    NarrationPlan plan;
    for (std::size_t i = 0; i < segments.size(); ++i) {
        auto f = fitter.fit(utts[i].measured_ms, segments[i].window_ms());
        if (drift.observe(f.pad_ms - segments[i].window_ms()))
            f = fitter.fit(utts[i].measured_ms, segments[i].window_ms());
        plan.items.push_back({i, f.atempo, segments[i].start_ms, f.overflow});
    }

    // 6. Render da narracao --------------------------------------------
    enter(Stage::Render);
    auto narration = render_narration(utts, plan, workdir);

    // 7. Stream + mux --------------------------------------------------
    enter(Stage::StreamMux);
    MuxSession sess;
    sess.source_argv = {"yt-dlp", "-f",
                        yt_dlp_format_selector(eff.max_height), url};
    sess.narration_path = narration.string();
    sess.player_argv = {eff.player, "--cache=yes", "--really-quiet", "-"};

    // v0.3 (roadmap item 4): re-spawn do mux em modo fifo. O pump mede a
    // banda real; prematuro => rede/yt-dlp caiu => tenta de novo com
    // backoff. Sem fifo_mode, comportamento e o de antes: falha rapida.
    const int attempts = eff.fifo_mode ? 1 + std::max(0, eff.mux_retries) : 1;
    int rc = 1;
    MuxHandle h;
    for (int a = 1; a <= attempts; ++a) {
        h = spawn_mux(sess);
        rc = h.wait_all();
        if (rc == 0 || !eff.fifo_mode || !h.premature_exit()) break;
        std::cerr << "[saci] mux caiu prematuramente; re-spawn " << a << "/"
                  << attempts - 1 << " (backoff " << 2 * a << "s)\n";
        ::sleep(static_cast<unsigned int>(2 * a));
    }

    // Persiste a estimativa de banda para a proxima sessao decidir o
    // degrau inicial com dados reais (1 arquivo pequeno — zero-disco ok).
    {
        std::ofstream be(workdir / "bw_estimate.txt");
        be << static_cast<long>(h.probe().estimate_kbps() + 0.5) << "\n";
    }

    enter(rc == 0 ? Stage::Done : Stage::Error);
    return rc;
}

} // namespace saci
