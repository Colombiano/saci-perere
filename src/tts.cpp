#include "saci/tts.hpp"

#include <cstdlib>
#include <sstream>
#include <stdexcept>

#include "saci/proc.hpp"

namespace saci {

PiperEngine::PiperEngine(std::filesystem::path binary,
                         std::filesystem::path voice_model)
    : bin_(std::move(binary)), voice_(std::move(voice_model)) {}

namespace {
// Mede duracao real via ffprobe: pior que assumir e melhor que adivinhar.
long probe_duration_ms(const std::filesystem::path& wav) {
    auto [code, out] = run_capture({"ffprobe", "-v", "error", "-show_entries",
                                    "format=duration", "-of", "csv=p=0",
                                    wav.string()});
    if (code != 0) return -1;
    try {
        return static_cast<long>(std::stod(out) * 1000.0);
    } catch (...) {
        return -1;
    }
}
} // namespace

Utterance PiperEngine::synthesize(std::size_t index,
                                  const std::string& text,
                                  const std::filesystem::path& outdir) const {
    std::filesystem::create_directories(outdir);
    auto wav = outdir / ("utt_" + std::to_string(index) + ".wav");

    int code = run_quiet({bin_.string(), "--model", voice_.string(),
                          "--output_file", wav.string(), "--stdin"});
    (void)text;  // esqueleto: piper le o texto de stdin; run_quiet2 faria pipe.
    if (code != 0)
        throw std::runtime_error("piper falhou na fala " + std::to_string(index));

    long ms = probe_duration_ms(wav);
    if (ms < 0) ms = static_cast<long>(text.size()) * 55;  // heuristica ~180 wpm
    return Utterance{index, wav, ms};
}

std::filesystem::path render_narration(const std::vector<Utterance>& utts,
                                       const NarrationPlan& plan,
                                       const std::filesystem::path& outdir) {
    std::filesystem::create_directories(outdir);
    auto out = outdir / "narracao.mp3";

    // filter_complex:
    //   [i:a]atempo=r[i_f]; [i_f]adelay=D|D[i_d]; ... ; [d0][d1]...amix=N:normalize=0[a]
    std::ostringstream fc;
    std::vector<std::string> mix_ins;
    for (const auto& it : plan.items) {
        std::string tag = "s" + std::to_string(it.index);
        fc << "[" << it.index << ":a]atempo=" << it.atempo << "[" << tag << "_t];";
        fc << "[" << tag << "_t]adelay=" << it.delay_ms << "|" << it.delay_ms
           << "[" << tag << "_d];";
        mix_ins.push_back("[" + tag + "_d]");
    }
    for (const auto& m : mix_ins) fc << m;
    fc << "amix=inputs=" << mix_ins.size()
       << ":normalize=0,loudnorm=I=-16:TP=-1.5[aout]";

    std::vector<std::string> argv = {"ffmpeg", "-y", "-hide_banner",
                                     "-loglevel", "error"};
    for (const auto& u : utts)
        argv.insert(argv.end(), {"-i", u.wav.string()});
    argv.insert(argv.end(), {"-filter_complex", fc.str(), "-map", "[aout]",
                             "-c:a", "libmp3lame", "-q:a", "4", out.string()});

    int code = run_quiet(argv);
    if (code != 0) throw std::runtime_error("render_narration: ffmpeg falhou");
    return out;
}

} // namespace saci
