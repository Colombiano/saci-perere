#include "saci/orchestrator.hpp"

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

    // 1. Legendas ------------------------------------------------------
    enter(Stage::FetchSubs);
    std::vector<std::string> dl = {"yt-dlp",
        "--write-subs", "--write-auto-subs",
        "--sub-langs", cfg_.sub_lang_pref,
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

    // 3. Traducao ------------------------------------------------------
    enter(Stage::Translate);
    ArgosEngine tr("en", cfg_.target_lang);
    std::vector<std::string> pt;
    pt.reserve(segments.size());
    for (auto& s : segments) pt.push_back(tr.translate(s.text));

    // 4. TTS -----------------------------------------------------------
    enter(Stage::Tts);
    PiperEngine piper(cfg_.tts_bin, cfg_.tts_voice);
    std::vector<Utterance> utts;
    utts.reserve(segments.size());
    for (std::size_t i = 0; i < segments.size(); ++i)
        utts.push_back(piper.synthesize(i, pt[i], workdir / "utt"));

    // 5. Plano de sincronia --------------------------------------------
    enter(Stage::SyncFit);
    SyncFitter fitter(SyncPolicy{cfg_.tempo_min, cfg_.tempo_max,
                                 cfg_.drift_threshold_ms});
    SyncFitter::DriftCorrector drift(cfg_.drift_threshold_ms);
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
                        yt_dlp_format_selector(cfg_.max_height), url};
    sess.narration_path = narration.string();
    sess.player_argv = {cfg_.player, "--cache=yes", "--really-quiet", "-"};
    auto h = spawn_mux(sess);
    int rc = h.wait_all();

    enter(rc == 0 ? Stage::Done : Stage::Error);
    return rc;
}

} // namespace saci
