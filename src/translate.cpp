#include "saci/translate.hpp"

#include <sstream>
#include <stdexcept>

#include "saci/proc.hpp"

namespace saci {

ArgosEngine::ArgosEngine(std::string from, std::string to, std::string cmd)
    : from_(std::move(from)), to_(std::move(to)), cmd_(std::move(cmd)) {}

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
} // namespace

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
    // Caminho rapido: uma invocacao so. SRT e por construcao texto de uma
    // linha (o parser cola linhas com espaco), entao '\n' e separador seguro.
    std::string joined;
    for (const auto& t : texts) {
        for (char c : t) joined += (c == '\n' || c == '\r') ? ' ' : c;
        joined += '\n';
    }
    auto [code, out] = run_capture2({cmd_, "--from-lang", from_,
                                     "--to-lang", to_}, joined);
    if (code == 0) {
        auto lines = split_lines(out);
        if (lines.size() == texts.size()) return lines;  // caminho rapido OK
    }

    // Caminho lento e correto: um processo por segmento. Lento porque
    // paga o startup do tradutor N vezes, mas preserva 1:1 com as janelas.
    std::vector<std::string> res;
    res.reserve(texts.size());
    for (const auto& t : texts) res.push_back(translate(t));
    return res;
}

} // namespace saci
