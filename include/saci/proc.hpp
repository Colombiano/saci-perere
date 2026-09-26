#pragma once
#include <string>
#include <utility>
#include <vector>

namespace saci {

// Executa argv, captura stdout. {exit_code, stdout}
std::pair<int, std::string> run_capture(const std::vector<std::string>& argv);
int run_quiet(const std::vector<std::string>& argv);

} // namespace saci
