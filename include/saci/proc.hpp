#pragma once
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace saci {

// Executa argv, captura stdout. {exit_code, stdout}
std::pair<int, std::string> run_capture(const std::vector<std::string>& argv);
int run_quiet(const std::vector<std::string>& argv);

// Executa argv ALIMENTANDO stdin e capturando stdout (pipe bidirecional).
// {exit_code, stdout}. Evita deadlock via poll(): escreve e le
// intercaladamente — seguro para entradas maiores que o buffer do pipe
// (buffer típico de 64 KiB; o selftest exerce 300 KiB).
//
// max_output protege a memória: estourar o limite mata o filho e lança.
// Contrato dos motores: translate(text) / synthesize(text) viajam por aqui.
std::pair<int, std::string> run_capture2(const std::vector<std::string>& argv,
                                         const std::string& input,
                                         std::size_t max_output = 64 << 20);

// Autoteste do run_capture2 (sem dependências externas além de cat/echo).
// ./build/saci --selftest  ->  exit 0 se tudo passar.
int selftest();

} // namespace saci
