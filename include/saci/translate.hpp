#pragma once
// Ontologia: TRADUCAO (mapeamento fiel, sem reuso de janela temporal).
#include <string>
#include <vector>

namespace saci {

template <typename E>
concept TranslationEngine = requires(E e, const std::string& s) {
    { e.translate(s) } -> std::convertible_to<std::string>;
};

// Tradutor via CLI `argos-translate` (offline). Alternativa: LLM externo
// (mesma assinatura conceitual; trocavel por policy em Lua).
class ArgosEngine {
public:
    ArgosEngine(std::string from, std::string to);

    std::string translate(const std::string& text) const;

private:
    std::string from_, to_;
};

} // namespace saci
