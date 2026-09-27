#include "saci/proc.hpp"

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>

#include "saci/bw_probe.hpp"
#include "saci/config.hpp"

namespace {

void exec_or_die(const std::vector<std::string>& argv) {
    std::vector<std::string> mut = argv;
    std::vector<char*> ptrs;
    ptrs.reserve(mut.size() + 1);
    for (auto& s : mut) ptrs.push_back(s.data());
    ptrs.push_back(nullptr);
    ::execvp(ptrs[0], ptrs.data());
    _exit(127);  // exec falhou
}

} // namespace

namespace saci {

std::pair<int, std::string> run_capture(const std::vector<std::string>& argv) {
    int fds[2];
    if (::pipe(fds) == -1) throw std::runtime_error(std::strerror(errno));

    pid_t pid = ::fork();
    if (pid == -1) throw std::runtime_error(std::strerror(errno));
    if (pid == 0) {
        ::close(fds[0]);
        ::dup2(fds[1], STDOUT_FILENO);
        ::close(fds[1]);
        exec_or_die(argv);
    }
    ::close(fds[1]);

    std::string out;
    std::array<char, 65536> buf{};
    for (;;) {
        ssize_t n = ::read(fds[0], buf.data(), buf.size());
        if (n > 0) out.append(buf.data(), static_cast<std::size_t>(n));
        else if (n == 0) break;
        else if (errno != EINTR) break;
    }
    ::close(fds[0]);

    int status = 0;
    ::waitpid(pid, &status, 0);
    int code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    return {code, std::move(out)};
}

int run_quiet(const std::vector<std::string>& argv) {
    pid_t pid = ::fork();
    if (pid == -1) throw std::runtime_error(std::strerror(errno));
    if (pid == 0) {
        int devnull = ::open("/dev/null", O_WRONLY);
        if (devnull != -1) { ::dup2(devnull, STDOUT_FILENO); ::dup2(devnull, STDERR_FILENO); }
        exec_or_die(argv);
    }
    int status = 0;
    ::waitpid(pid, &status, 0);
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

std::pair<int, std::string> run_capture2(const std::vector<std::string>& argv,
                                         const std::string& input,
                                         std::size_t max_output) {
    int in_fds[2], out_fds[2];
    if (::pipe(in_fds) == -1 || ::pipe(out_fds) == -1)
        throw std::runtime_error(std::strerror(errno));

    pid_t pid = ::fork();
    if (pid == -1) throw std::runtime_error(std::strerror(errno));
    if (pid == 0) {
        ::dup2(in_fds[0], STDIN_FILENO);
        ::dup2(out_fds[1], STDOUT_FILENO);
        // stderr continua no terminal: diagnóstico do subprocesso visível.
        ::close(in_fds[0]); ::close(in_fds[1]);
        ::close(out_fds[0]); ::close(out_fds[1]);
        exec_or_die(argv);
    }
    ::close(in_fds[0]);
    ::close(out_fds[1]);

    // Lados do PAI nao-bloqueantes: escrevemos/lemos ate EAGAIN e voltamos
    // ao poll. Com fds bloqueantes um write grande travaria para sempre
    // quando o filho, ecoando, encher o proprio stdout (deadlock real,
    // pego pelo selftest de 300 KiB na v0.2).
    ::fcntl(in_fds[1], F_SETFL, ::fcntl(in_fds[1], F_GETFL) | O_NONBLOCK);
    ::fcntl(out_fds[0], F_SETFL, ::fcntl(out_fds[0], F_GETFL) | O_NONBLOCK);

    std::string out;
    out.reserve(std::min(input.size(), max_output));
    std::size_t written = 0;
    bool in_open = true, out_open = true;
    std::array<char, 65536> buf{};

    while (in_open || out_open) {
        ::pollfd pfds[2];
        pfds[0] = {out_fds[0], out_open ? short(POLLIN) : short(0), 0};
        pfds[1] = {in_fds[1],  in_open  ? short(POLLOUT) : short(0), 0};
        int r = ::poll(pfds, 2, -1);
        if (r == -1) {
            if (errno == EINTR) continue;
            break;
        }

        if (out_open && (pfds[0].revents & (POLLIN | POLLHUP))) {
            for (;;) {  // drena tudo o que estiver disponivel
                ssize_t n = ::read(out_fds[0], buf.data(), buf.size());
                if (n > 0) {
                    if (out.size() + static_cast<std::size_t>(n) > max_output) {
                        ::kill(pid, SIGKILL);
                        ::waitpid(pid, nullptr, 0);
                        ::close(out_fds[0]);
                        if (in_open) ::close(in_fds[1]);
                        throw std::length_error("run_capture2: stdout estourou max_output");
                    }
                    out.append(buf.data(), static_cast<std::size_t>(n));
                } else if (n == 0) {
                    out_open = false; ::close(out_fds[0]); break;
                } else if (errno == EINTR) {
                    continue;
                } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    break;
                } else {
                    out_open = false; ::close(out_fds[0]); break;
                }
            }
        }

        if (in_open && (pfds[1].revents & (POLLOUT | POLLERR | POLLHUP))) {
            while (written < input.size()) {  // enche ate o pipe aceitar
                ssize_t n = ::write(in_fds[1], input.data() + written,
                                    input.size() - written);
                if (n > 0) { written += static_cast<std::size_t>(n); continue; }
                if (n == -1 && errno == EINTR) continue;
                break;  // EAGAIN: buffer cheio — poll decide quando voltar
            }
            if (written >= input.size() || (pfds[1].revents & (POLLERR | POLLHUP))) {
                in_open = false;
                ::close(in_fds[1]);  // EOF pro filho
            }
        }
    }

    int status = 0;
    ::waitpid(pid, &status, 0);
    int code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    return {code, std::move(out)};
}

int selftest() {
    bool ok = true;
    auto check = [&](const char* nome, bool pass, std::string extra = "") {
        std::cerr << "[selftest] " << (pass ? "PASS " : "FAIL ")
                  << nome << (extra.empty() ? "" : "  (" + extra + ")") << "\n";
        ok = ok && pass;
    };

    // 1) eco bidirecional simples
    {
        auto [code, out] = run_capture2({"cat"}, "saci-perere\n");
        check("cat ecoa stdin", code == 0 && out == "saci-perere\n",
              "exit=" + std::to_string(code));
    }
    // 2) entrada de 300 KiB — maior que o buffer do pipe; sem poll() isto
    //    daria deadlock garantido (cat ecoa e bloqueia, pai bloqueia na escrita)
    {
        std::string big(300 * 1024, 'x');
        auto [code, out] = run_capture2({"cat"}, big);
        check("cat ecoa 300 KiB (sem deadlock)", code == 0 && out == big,
              "recebidos=" + std::to_string(out.size()));
    }
    // 3) sem entrada: processo que so imprime
    {
        auto [code, out] = run_capture2({"echo", "ok"}, "");
        check("echo sem stdin", code == 0 && out.find("ok") != std::string::npos);
    }
#ifdef SACI_WITH_LUA
    // 4) config via Lua: o script RETORNA a tabela que preenche Config.
    //    (rode da raiz do repo: ./build/saci --selftest)
    try {
        Config c = Config::load("lua/default_config.lua");
        check("config Lua (tabela retornada preenche Config)",
              c.target_lang == "pt-BR" && c.source_lang == "en" &&
              c.max_height == 360 && c.sites.empty());
    } catch (const std::exception& e) {
        check("config Lua (tabela retornada preenche Config)", false, e.what());
    }
#endif
    // 5) FFT: senoide pura (bin 8) -> periodicidade alta ---------------
    {
        BandwidthProbe p;
        constexpr double pi = 3.14159265358979323846;
        for (std::size_t t = 0; t < 64; ++t) {
            const double kbps = 800.0 + 400.0 * std::cos(2.0 * pi * 8.0 * static_cast<double>(t) / 64.0);
            p.sample_window(static_cast<std::uint64_t>(kbps * 125.0), 1.0);
        }
        check("FFT: ciclo forte detectado", p.periodicity() > 0.85,
              "periodicidade=" + std::to_string(p.periodicity()));
    }
    // 6) estimador robusto (p25 x margem) + FFT ignora constante ---------
    {
        BandwidthProbe p;
        for (int t = 0; t < 64; ++t) p.sample_window(100000, 1.0);  // 800 kbps
        check("estimador: 800 kbps -> 600 (p25 x 0.75)",
              std::abs(p.estimate_kbps() - 600.0) < 1.0,
              "estimado=" + std::to_string(p.estimate_kbps()));
        check("FFT: sinal constante nao e ciclico (DC removido)",
              p.periodicity() < 0.2,
              "periodicidade=" + std::to_string(p.periodicity()));
    }
    // 7) Haar: queda brusca de vazao aciona drop_detected ----------------
    {
        BandwidthProbe p;
        for (int t = 0; t < 32; ++t) p.sample_window(100000, 1.0);   // 800 kbps
        bool drop = false;
        for (int t = 0; t < 8; ++t) {                               // cai p/ 240
            p.sample_window(30000, 1.0);
            drop = drop || p.drop_detected();
        }
        check("Haar: queda brusca dispara drop", drop);
    }
    return ok ? 0 : 1;
}

} // namespace saci
