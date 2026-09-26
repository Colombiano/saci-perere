#pragma once
// Ontologia: CONFIGURACAO (politicas declarativas).
// Fonte de verdade: script Lua (SACI_WITH_LUA) ou fallback chave=valor.
// v0.2: politicas POR-SITE (tabelas em Lua; ver lua/default_config.lua).
#include <filesystem>
#include <string>
#include <vector>

namespace saci {

// Politica local: sobrescreve campos do Config global quando a URL casa
// com `pattern` (substring simples; primeira regra que casa vence).
// Campo vazio/zero = "herda do global".
struct SitePolicy {
    std::string pattern;        // ex.: "youtube.com", "example.com/aulas"
    int max_height = 0;         // degrau de video para este site
    std::string sub_lang_pref;  // regex de legendas preferidas
    std::string target_lang;    // idioma destino (hoje pt-BR; es/zh no roadmap)
    std::string tts_voice;      // modelo de voz por canal
};

struct Config {
    // midia
    int max_height = 360;
    std::string player = "mpv";

    // legenda / traducao
    std::string sub_lang_pref = "en.*";   // regex de idiomas aceitos
    std::string source_lang = "en";       // lingua de origem da legenda
    std::string target_lang = "pt-BR";    // proximas versoes: es, zh, ...

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

    // politicas por-site (Lua; vazio no fallback chave=valor)
    std::vector<SitePolicy> sites;

    static Config load(const std::filesystem::path& file);

    // Devolve uma COPIA do config com a primeira regra de site que casar
    // com a URL aplicada por cima dos campos globais.
    Config effective_for(const std::string& url) const;
};

} // namespace saci
