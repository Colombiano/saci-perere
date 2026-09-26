#pragma once
// Ontologia: MAQUINA DE ESTADOS DO PIPELINE.
#include <filesystem>
#include <string>

#include "saci/config.hpp"

namespace saci {

enum class Stage {
    Idle,
    FetchSubs,    // yt-dlp --write-subs --skip-download
    Parse,        // SRT -> vector<TranscriptSegment>
    Translate,    // en -> pt-BR (janelas preservadas)
    Tts,          // Piper por segmento -> vector<Utterance>
    SyncFit,      // SyncFitter + DriftCorrector -> NarrationPlan
    Render,       // ffmpeg filter_complex -> narracao.mp3
    StreamMux,    // yt-dlp | ffmpeg | player (pipes, zero-disco)
    Done,
    Error
};

const char* stage_name(Stage s);

class Orchestrator {
public:
    explicit Orchestrator(Config cfg) : cfg_(std::move(cfg)) {}

    // Executa o pipeline completo. Pode relancar excecoes das etapas.
    int run(const std::string& url, const std::filesystem::path& workdir);

    Stage stage() const { return stage_; }

private:
    void enter(Stage s);

    Config cfg_;
    Stage stage_ = Stage::Idle;
};

} // namespace saci
