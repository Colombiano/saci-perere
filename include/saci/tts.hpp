#pragma once
// Ontologia: LOCUCAO (utterance) e TRILHA DE NARRACAO.
#include <filesystem>
#include <string>
#include <vector>

namespace saci {

// Audio TTS de UMA fala, com duracao medida (nao assumida).
struct Utterance {
    std::size_t index = 0;
    std::filesystem::path wav;
    long measured_ms = 0;   // medido via ffprobe sobre o WAV gerado
};

// Motor TTS: Piper (neural, tempo real em CPU fraca). CLI: piper --model ...
class PiperEngine {
public:
    PiperEngine(std::filesystem::path binary,
                std::filesystem::path voice_model);

    Utterance synthesize(std::size_t index,
                         const std::string& text,
                         const std::filesystem::path& outdir) const;

private:
    std::filesystem::path bin_, voice_;
};

// Monta narracao.mp3: concatena locucoes posicionadas por adelay,
// com atempo por fala e loudnorm final. Gera filter_complex do ffmpeg.
struct NarrationPlan {
    struct Item {
        std::size_t index;
        double atempo = 1.0;        // ja limitado a [tempo_min, tempo_max]
        std::int64_t delay_ms = 0;  // start_ms da janela
        bool overflow = false;      // fala nao coube nem no limite
    };
    std::vector<Item> items;
};

std::filesystem::path render_narration(const std::vector<Utterance>& utts,
                                       const NarrationPlan& plan,
                                       const std::filesystem::path& outdir);

} // namespace saci
