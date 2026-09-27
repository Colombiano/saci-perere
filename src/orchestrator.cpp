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

#ifdef SACI_WITH_LUA
#include <sol/sol.hpp>
#endif

namespace saci {

// Hooks Lua (v0.4): on_stage(nome, ms) a cada troca de etapa.
// pimpl para o header nao depender de sol2.
struct Orchestrator::Hooks {
#ifdef SACI_WITH_LUA
    sol::state lua;
    bool ok = false;
#endif
};

Orchestrator::Orchestrator(Config cfg) : cfg_(std::move(cfg)) {}
Orchestrator::~Orchestrator() = default;
Orchestrator::Orchestrator(Orchestrator&&) noexcept = default;
Orchestrator& Orchestrator::operator=(Orchestrator&&) noexcept = default;

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
    const auto now = std::chrono::steady_clock::now();
    const long ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        now - last_).count();
    last_ = now;
    stage_ = s;
    std::cerr << "[saci] etapa: " << stage_name(s) << " (+" << ms << " ms)\n";
#ifdef SACI_WITH_LUA
    // Hook de usuario NUNCA derruba o pipeline: excecao aqui e engolida.
    if (hooks_ && hooks_->ok) {
        try {
            hooks_->lua["on_stage"](stage_name(s), ms);
        } catch (...) { /* o show e do video, nao do hook */ }
    }
#endif
}

int Orchestrator::run(const std::string& url,
                      const std::filesystem::path& workdir) {
    using namespace std::filesystem;
    create_directories(workdir);

    // v0.2: politicas por-site — o config efetivo mistura o global com a
    // primeira regra de site que casa com a URL (ver Config::effective_for).
    const Config eff = cfg_.effective_for(url);

#ifdef SACI_WITH_LUA
    // v0.4: hooks Lua opcionais (on_stage por etapa; erro nao derruba nada)
    if (!eff.hooks_file.empty()) {
        try {
            hooks_ = std::make_unique<Hooks>();
            hooks_->lua.open_libraries(sol::lib::base);
            hooks_->lua.script_file(eff.hooks_file);
            hooks_->ok = true;
        } catch (const std::exception& e) {
            std::cerr << "[saci] hooks nao carregados: " << e.what() << "\n";
            hooks_.reset();
        }
    }
#endif

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

    // v0.4: o probe da sessao anterior vira degrau concreto — a escada
    // consteval de stream.hpp (ou a tabela `ladder` da Lua) escolhe o
    // maior degrau que cabe na estimativa; sem estimativa, segue o config.
    int cap = eff.max_height;
    {
        std::ifstream be(workdir / "bw_estimate.txt");
        double est = 0;
        be >> est;
        if (est > 0) {
            QualityLadder ladder = eff.ladder.empty()
                                       ? QualityLadder::with_defaults()
                                       : QualityLadder{eff.ladder};
            cap = std::min(cap, ladder.select(static_cast<std::int64_t>(est)).height);
            std::cerr << "[saci] estimativa anterior " << static_cast<long>(est)
                      << " kbps -> degrau " << cap << "p\n";
        }
    }

    // designated initializers (C++20): sessao declarada de uma vez
    MuxSession sess{.source_argv = {"yt-dlp", "-f",
                                    yt_dlp_format_selector(cap), url},
                    .narration_path = narration.string(),
                    .player_argv = {eff.player, "--cache=yes",
                                    "--really-quiet", "-"},
                    .direct_url = "",
                    .source_retries = 0};

    // v0.5: URL direta (yt-dlp -g) alimenta o resume fino por Range.
    // Só resolve se for usar (fifo + retries); senão, re-spawn completo.
    if (eff.fifo_mode && eff.mux_retries > 0) {
        auto [rc, out] = run_capture({"yt-dlp", "-g", "-f",
                                      yt_dlp_format_selector(cap), url});
        if (rc == 0 && !out.empty()) {
            sess.direct_url = out.substr(0, out.find_first_of("\r\n"));
            sess.source_retries = eff.mux_retries;
            std::cerr << "[saci] URL direta resolvida; resume fino ativo\n";
        } else {
            std::cerr << "[saci] yt-dlp -g falhou; resume fino desativado\n";
        }
    }

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
