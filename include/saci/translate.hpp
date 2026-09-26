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
// A partir da v0.2 o texto viaja de verdade: run_capture2 alimenta o stdin
// do tradutor e captura o resultado (roadmap #1 concluido).
class ArgosEngine {
public:
    ArgosEngine(std::string from, std::string to,
                std::string cmd = "argos-translate");

    std::string translate(const std::string& text) const;

    // Traducao em LOTE (roadmap #2): uma unica invocacao para todo o SRT.
    // Estrategia: cola os textos com '\n' numa passada so. Se o CLI
    // devolver a mesma quantidade de linhas, otimo (caminho rapido); se
    // nao (modo paragrafo), cai no caminho lento e correto: segmento a
    // segmento. Em ambos os casos as janelas SRT ficam intactas.
    std::vector<std::string> translate_batch(const std::vector<std::string>& texts) const;

private:
    std::string from_, to_, cmd_;
};

} // namespace saci
