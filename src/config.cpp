#include "saci/config.hpp"

#include <fstream>
#include <sstream>
#include <stdexcept>

namespace {

// Fallback sem Lua: formato simples "chave = valor", '#' comenta.
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
        else if (k == "sub_lang_pref") c.sub_lang_pref = v;
        else if (k == "target_lang") c.target_lang = v;
        else if (k == "tts_bin") c.tts_bin = v;
        else if (k == "tts_voice") c.tts_voice = v;
        else if (k == "translate_cmd") c.translate_cmd = v;
        else if (k == "tempo_min") c.tempo_min = std::stod(v);
        else if (k == "tempo_max") c.tempo_max = std::stod(v);
        else if (k == "drift_threshold_ms") c.drift_threshold_ms = std::stol(v);
        else if (k == "ring_bytes") c.ring_bytes = std::stoll(v);
        else if (k == "fifo_mode") c.fifo_mode = (v == "true" || v == "1");
    }
    return c;
}

} // namespace

namespace saci {

Config Config::load(const std::filesystem::path& file) {
#ifdef SACI_WITH_LUA
    sol::state lua;
    lua.open_libraries(sol::lib::base);
    lua.script_file(file.string());
    Config c;
    c.max_height        = lua.get_or("max_height", 360);
    c.player            = lua.get_or<std::string>("player", "mpv");
    c.sub_lang_pref     = lua.get_or<std::string>("sub_lang_pref", "en.*");
    c.target_lang       = lua.get_or<std::string>("target_lang", "pt-BR");
    c.tts_bin           = lua.get_or<std::string>("tts_bin", "piper");
    c.tts_voice         = lua.get_or<std::string>("tts_voice", "");
    c.translate_cmd     = lua.get_or<std::string>("translate_cmd", "argos-translate");
    c.tempo_min         = lua.get_or("tempo_min", 0.85);
    c.tempo_max         = lua.get_or("tempo_max", 1.30);
    c.drift_threshold_ms= lua.get_or("drift_threshold_ms", 300L);
    c.ring_bytes        = lua.get_or("ring_bytes", std::int64_t{50} << 20);
    c.fifo_mode         = lua.get_or("fifo_mode", false);
    return c;
#else
    std::ifstream in(file);
    if (!in) throw std::runtime_error("config nao encontrado: " + file.string());
    std::stringstream ss;
    ss << in.rdbuf();
    return parse_kv(ss.str());
#endif
}

} // namespace saci
