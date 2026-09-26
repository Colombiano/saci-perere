#pragma once
// Ontologia: CONFIGURACAO (politicas declarativas).
// Fonte de verdade: script Lua (SACI_WITH_LUA) ou fallback chave=valor.
#include <filesystem>
#include <string>

namespace saci {

struct Config {
    // midia
    int max_height = 360;
    std::string player = "mpv";

    // legenda / traducao
    std::string sub_lang_pref = "en.*";   // regex de idiomas aceitos
    std::string target_lang = "pt-BR";

    // tts
    std::filesystem::path tts_bin = "piper";
    std::filesystem::path tts_voice;       // modelo .onnx (voz pt-BR)

    // traducao
    std::string translate_cmd = "argos-translate";

    // sincronia (ver Ontologia: invariantes)
    double tempo_min = 0.85;
    double tempo_max = 1.30;
    long drift_threshold_ms = 300;

    // rede / disco
    std::int64_t ring_bytes = 50 * 1024 * 1024;  // buffer de prefetch
    bool fifo_mode = false;                      // true => sobrevive a quedas

    static Config load(const std::filesystem::path& file);
};

} // namespace saci
