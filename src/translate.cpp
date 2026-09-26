#include "saci/translate.hpp"

#include <stdexcept>

#include "saci/proc.hpp"

namespace saci {

ArgosEngine::ArgosEngine(std::string from, std::string to)
    : from_(std::move(from)), to_(std::move(to)) {}

std::string ArgosEngine::translate(const std::string& text) const {
    // argos-translate traduz de stdin para stdout.
    auto [code, out] = run_capture({"argos-translate", "--from-lang", from_,
                                    "--to-lang", to_});
    (void)text;  // esqueleto: CLI real leria do stdin; aqui documentamos o
                 // contrato. Substituir por pipe bidirecional em run_capture2.
    if (code != 0)
        throw std::runtime_error("argos-translate falhou (exit " +
                                 std::to_string(code) + ")");
    return out;
}

} // namespace saci
