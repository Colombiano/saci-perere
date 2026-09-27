#include "saci/translate.hpp"

#include <sstream>
#include <stdexcept>

#include "saci/proc.hpp"

namespace saci {

namespace {
std::vector<std::string> split_lines(const std::string& s) {
    std::vector<std::string> lines;
    std::istringstream in(s);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(std::move(line));
    }
    return lines;
}

// Argos usa codigo primario ("pt", nao "pt-BR"): o modelo en->pt JA e o
// portugues brasileiro. Config fica amigavel (pt-BR), CLI recebe o subtag.
std::string cli_lang(std::string code) {
    const auto dash = code.find('-');
    return dash == std::string::npos ? code : code.substr(0, dash);
}
} // namespace

ArgosEngine::ArgosEngine(std::string from, std::string to, std::string cmd,
                         std::string bridge)
    : from_(cli_lang(std::move(from))), to_(cli_lang(std::move(to))),
      cmd_(std::move(cmd)), bridge_(std::move(bridge)) {}

std::string ArgosEngine::translate(const std::string& text) const {
    // argos-translate traduz de stdin para stdout.
    auto [code, out] = run_capture2({cmd_, "--from-lang", from_,
                                     "--to-lang", to_}, text + "\n");
    if (code != 0)
        throw std::runtime_error("argos-translate falhou (exit " +
                                 std::to_string(code) + ")");
    auto lines = split_lines(out);
    return lines.empty() ? "" : lines.front();
}

std::vector<std::string>
ArgosEngine::translate_batch(const std::vector<std::string>& texts) const {
    // SRT e por construcao texto de uma linha (o parser cola linhas com
    // espaco), entao '\n' e separador seguro.
    std::string joined;
    for (const auto& t : texts) {
        for (char c : t) joined += (c == '\n' || c == '\r') ? ' ' : c;
        joined += '\n';
    }

    // Caminho rapido de verdade (v0.6): a ponte Python carrega o modelo
    // UMA vez para todos os segmentos. Config: translate_bridge.
    if (!bridge_.empty()) {
        auto [code, out] = run_capture2({bridge_, from_, to_}, joined);
        if (code == 0) {
            auto lines = split_lines(out);
            if (lines.size() == texts.size()) return lines;  // ponte OK
        }
        // ponte falhou ou contagem estranha: cai no CLI abaixo
    }

    // Caminho rapido CLI: uma invocacao so; se o CLI devolver a mesma
    // quantidade de linhas, otimo.
    auto [code, out] = run_capture2({cmd_, "--from-lang", from_,
                                     "--to-lang", to_}, joined);
    if (code == 0) {
        auto lines = split_lines(out);
        if (lines.size() == texts.size()) return lines;
    }

    // Caminho lento e correto: um processo por segmento. Lento porque
    // paga o startup do tradutor N vezes, mas preserva 1:1 com as janelas.
    std::vector<std::string> res;
    res.reserve(texts.size());
    for (const auto& t : texts) res.push_back(translate(t));
    return res;
}

QwenEngine::QwenEngine(std::string cmd, std::string model,
                       std::string from, std::string to)
    : cmd_(std::move(cmd)), model_(std::move(model)),
      from_(std::move(from)), to_(std::move(to)) {}

std::string QwenEngine::translate(const std::string& text) const {
    // Prompt curto e fechado: LLM generativo tende a discorrer; pedimos
    // so a traducao para a saida ser limpa para o TTS.
    const std::string prompt =
        "Traduza de " + from_ + " para " + to_ +
        ". Responda APENAS a traducao, sem explicacoes:\n" + text;
    auto [code, out] = run_capture2({cmd_, "run", model_}, prompt + "\n");
    if (code != 0)
        throw std::runtime_error("LLM falhou (exit " + std::to_string(code) + ")");
    // Uma linha de saida por prompt; \n final vira espaco inocuo no TTS.
    std::string cleaned;
    for (char c : out) cleaned += (c == '\n' || c == '\r') ? ' ' : c;
    while (!cleaned.empty() && cleaned.back() == ' ') cleaned.pop_back();
    return cleaned.empty() ? text : cleaned;
}

std::vector<std::string>
QwenEngine::translate_batch(const std::vector<std::string>& texts) const {
    // Sem caminho rapido de proposito: LLM nao garante contagem 1:1.
    std::vector<std::string> res;
    res.reserve(texts.size());
    for (const auto& t : texts) res.push_back(translate(t));
    return res;
}

} // namespace saci
