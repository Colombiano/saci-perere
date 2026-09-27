#pragma once
// Ontologia: MAQUINA DE ESTADOS DO PIPELINE.
#include <chrono>
#include <filesystem>
#include <memory>
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
    explicit Orchestrator(Config cfg);
    ~Orchestrator();
    Orchestrator(Orchestrator&&) noexcept;
    Orchestrator& operator=(Orchestrator&&) noexcept;

    // Executa o pipeline completo. Pode relancar excecoes das etapas.
    int run(const std::string& url, const std::filesystem::path& workdir);

    Stage stage() const { return stage_; }

private:
    void enter(Stage s);

    struct Hooks;                  // pimpl: sol::state mora no .cpp (v0.4)
    std::unique_ptr<Hooks> hooks_; // hooks Lua opcionais (on_stage)
    Config cfg_;
    Stage stage_ = Stage::Idle;
    std::chrono::steady_clock::time_point last_ = std::chrono::steady_clock::now();
};

} // namespace saci
