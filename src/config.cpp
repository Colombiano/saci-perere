#include "saci/config.hpp"

#include <fstream>
#include <sstream>
#include <stdexcept>

#ifdef SACI_WITH_LUA
#include <sol/sol.hpp>
#endif

namespace {

#ifndef SACI_WITH_LUA
// Fallback sem Lua: formato simples "chave = valor", '#' comenta.
// Politicas por-site exigem Lua (tabelas aninhadas) — de propósito:
// incentiva o script como fonte de verdade rica.
saci::Config parse_kv(const std::string& text) {
    saci::Config c;
    std::istringstream in(text);
    std::string line;
    auto trim = [](std::string s) {
        auto i = s.find_first_not_of(" \t");
        auto j = s.find_last_not_of(" \t");
        return i == std::string::npos ? "" : s.substr(i, j - i + 1);
    };
    while (std::getline(in, line)) {
        auto hash = line.find('#');
        if (hash != std::string::npos) line = line.substr(0, hash);
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = trim(line.substr(0, eq));
        std::string v = trim(line.substr(eq + 1));
        if (k == "max_height") c.max_height = std::stoi(v);
        else if (k == "player") c.player = v;
        else if (k == "yt_dlp") c.yt_dlp = v;
        else if (k == "sub_lang_pref") c.sub_lang_pref = v;
        else if (k == "source_lang") c.source_lang = v;
        else if (k == "target_lang") c.target_lang = v;
        else if (k == "tts_bin") c.tts_bin = v;
        else if (k == "tts_voice") c.tts_voice = v;
        else if (k == "translate_cmd") c.translate_cmd = v;
        else if (k == "translate_bridge") c.translate_bridge = v;
        else if (k == "translate_backend") c.translate_backend = v;
        else if (k == "llm_cmd") c.llm_cmd = v;
        else if (k == "llm_model") c.llm_model = v;
        else if (k == "mux_retries") c.mux_retries = std::stoi(v);
        else if (k == "hooks_file") c.hooks_file = v;
        else if (k == "tempo_min") c.tempo_min = std::stod(v);
        else if (k == "tempo_max") c.tempo_max = std::stod(v);
        else if (k == "drift_threshold_ms") c.drift_threshold_ms = std::stol(v);
        else if (k == "ring_bytes") c.ring_bytes = std::stoll(v);
        else if (k == "fifo_mode") c.fifo_mode = (v == "true" || v == "1");
    }
    return c;
}
#endif // SACI_WITH_LUA

} // namespace

namespace saci {

Config Config::load(const std::filesystem::path& file) {
#ifdef SACI_WITH_LUA
    // v0.2: o script RETORNA a tabela de politicas (ver lua/default_config.lua).
    // Antes liamos variaveis globais — um `return {...}` nunca era visto e
    // os defaults de C++ sempre venciam. Agora a tabela retornada e a fonte.
    sol::state lua;
    lua.open_libraries(sol::lib::base, sol::lib::os);
    sol::table t = lua.script_file(file.string());

    Config c;
    c.max_height         = t.get_or("max_height", 360);
    c.player             = t.get_or<std::string>("player", "mpv");
    c.yt_dlp             = t.get_or<std::string>("yt_dlp", "yt-dlp");
    c.sub_lang_pref      = t.get_or<std::string>("sub_lang_pref", "en.*");
    c.source_lang        = t.get_or<std::string>("source_lang", "en");
    c.target_lang        = t.get_or<std::string>("target_lang", "pt-BR");
    c.tts_bin            = t.get_or<std::string>("tts_bin", "piper");
    c.tts_voice          = t.get_or<std::string>("tts_voice", "");
    c.translate_cmd      = t.get_or<std::string>("translate_cmd", "argos-translate");
    c.translate_bridge   = t.get_or<std::string>("translate_bridge", "");
    c.translate_backend  = t.get_or<std::string>("translate_backend", "argos");
    c.llm_cmd            = t.get_or<std::string>("llm_cmd", "ollama");
    c.llm_model          = t.get_or<std::string>("llm_model", "qwen2.5");
    c.mux_retries        = t.get_or("mux_retries", 3);
    c.hooks_file         = t.get_or<std::string>("hooks_file", "");
    c.tempo_min          = t.get_or("tempo_min", 0.85);
    c.tempo_max          = t.get_or("tempo_max", 1.30);
    c.drift_threshold_ms = t.get_or("drift_threshold_ms", 300L);
    c.ring_bytes         = t.get_or("ring_bytes", std::int64_t{50} << 20);
    c.fifo_mode          = t.get_or("fifo_mode", false);

    // ladder = { {height=..., video_kbps=...}, ... } — sobrescreve a
    // tabela consteval de stream.hpp; ordenacao garantida por height.
    sol::optional<sol::table> ladder = t["ladder"];
    if (ladder) {
        for (auto& [_, v] : ladder->pairs()) {
            sol::table rt = v.as<sol::table>();
            QualityRung r;
            r.height     = rt.get_or("height", 0);
            r.video_kbps = rt.get_or("video_kbps", 0);
            if (r.height > 0 && r.video_kbps > 0) c.ladder.push_back(r);
        }
        std::ranges::sort(c.ladder, {}, &QualityRung::height);
    }

    // sites = { {pattern=..., max_height=..., ...}, ... }
    sol::optional<sol::table> sites = t["sites"];
    if (sites) {
        for (auto& [_, v] : sites->pairs()) {
            sol::table st = v.as<sol::table>();
            SitePolicy sp;
            sp.pattern       = st.get_or<std::string>("pattern", "");
            sp.max_height    = st.get_or("max_height", 0);
            sp.sub_lang_pref = st.get_or<std::string>("sub_lang_pref", "");
            sp.target_lang   = st.get_or<std::string>("target_lang", "");
            sp.tts_voice     = st.get_or<std::string>("tts_voice", "");
            if (!sp.pattern.empty()) c.sites.push_back(std::move(sp));
        }
    }
    return c;
#else
    std::ifstream in(file);
    if (!in) throw std::runtime_error("config nao encontrado: " + file.string());
    std::stringstream ss;
    ss << in.rdbuf();
    return parse_kv(ss.str());
#endif
}

Config Config::effective_for(const std::string& url) const {
    Config eff = *this;  // copia; campos do site sobrescrevem por cima
    for (const auto& s : sites) {
        if (s.pattern.empty() || url.find(s.pattern) == std::string::npos)
            continue;
        if (s.max_height > 0)        eff.max_height = s.max_height;
        if (!s.sub_lang_pref.empty()) eff.sub_lang_pref = s.sub_lang_pref;
        if (!s.target_lang.empty())   eff.target_lang = s.target_lang;
        if (!s.tts_voice.empty())     eff.tts_voice = s.tts_voice;
        break;  // primeira regra que casa vence (ordem do script importa)
    }
    return eff;
}

} // namespace saci
