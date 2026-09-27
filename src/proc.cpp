#include "saci/proc.hpp"

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

#include "saci/bw_probe.hpp"
#include "saci/config.hpp"
#include "saci/muxer.hpp"
#include "saci/reactor.hpp"

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
    //    Localiza default_config.lua relativo ao executavel (build/ ou
    //    build-lua/ sob a raiz do repo), com fallback ao cwd — assim o
    //    selftest passa de qualquer diretorio (saci e' comando global).
    try {
        namespace fs = std::filesystem;
        fs::path script = "lua/default_config.lua";
        std::error_code ec;
        if (fs::path exe = fs::read_symlink("/proc/self/exe", ec); !ec) {
            fs::path near_exe = exe.parent_path() / ".." / script;
            if (fs::exists(near_exe, ec)) script = near_exe;
        }
        Config c = Config::load(script);
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
    // 8) Reactor: co_await REAL sobre pipe (v0.4, roadmap item 5) --------
    {
        int fds[2];
        if (::pipe(fds) == 0) {
            Reactor reactor;
            std::atomic<int> done{0};
            std::string got;
            std::jthread writer([&] {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                const ssize_t w = ::write(fds[1], "saci", 4);
                (void)w;
                ::close(fds[1]);
            });
            auto reader = [](Reactor& r, int fd, std::string& out,
                             std::atomic<int>& d) -> Task<int> {
                char buf[16]{};
                const ssize_t n = co_await Readable{r, fd, buf, sizeof buf};
                if (n > 0) out.append(buf, static_cast<std::size_t>(n));
                d.store(1);
                d.notify_all();
                co_return 0;
            };
            auto task = reader(reactor, fds[0], got, done);
            done.wait(0);  // atomic::wait — sem busy-loop
            ::close(fds[0]);
            check("reactor: co_await Readable retoma no evento", got == "saci",
                  "lido=" + got);
        } else {
            check("reactor: co_await Readable retoma no evento", false, "pipe falhou");
        }
    }
    // 9) pump_fds: fluxo simples (fonte unica, exit 0) -------------------
    {
        int f[2];
        if (::pipe(f) == 0) {
            ::fcntl(f[0], F_SETFD, ::fcntl(f[0], F_GETFD) | FD_CLOEXEC);
            ::fcntl(f[1], F_SETFD, ::fcntl(f[1], F_GETFD) | FD_CLOEXEC);
            Reactor reactor;
            BandwidthProbe probe;
            std::atomic<int> done{0};
            PumpOptions opts;
            opts.spawn_source = [](int, std::uint64_t) -> std::pair<pid_t, int> {
                int q[2];
                ::pipe(q);
                ::fcntl(q[0], F_SETFD, ::fcntl(q[0], F_GETFD) | FD_CLOEXEC);
                ::fcntl(q[1], F_SETFD, ::fcntl(q[1], F_GETFD) | FD_CLOEXEC);
                const pid_t pid = ::fork();
                if (pid == 0) {
                    ::dup2(q[1], STDOUT_FILENO);
                    ::execlp("sh", "sh", "-c", "head -c 100000 /dev/zero", nullptr);
                    _exit(127);
                }
                ::close(q[1]);
                return {pid, q[0]};
            };
            opts.max_source_retries = 0;
            const pid_t cons = ::fork();  // wc -c < f
            if (cons == 0) {
                ::dup2(f[0], STDIN_FILENO);
                ::execlp("wc", "wc", "-c", nullptr);
                _exit(127);
            }
            ::close(f[0]);
            auto task = pump_fds(reactor, f[1], probe, done, opts);
            done.wait(0);
            int stc = 0;
            ::waitpid(cons, &stc, 0);
            check("pump_fds: 100000 bytes numa fonte so", done.load() - 1 == 0,
                  "code=" + std::to_string(done.load() - 1));
        } else {
            check("pump_fds: 100000 bytes numa fonte so", false, "pipe falhou");
        }
    }
    // 10) RESUME FINO: fonte cai em 10000 e volta do byte exato ----------
    {
        int f[2];
        if (::pipe(f) == 0) {
            ::fcntl(f[0], F_SETFD, ::fcntl(f[0], F_GETFD) | FD_CLOEXEC);
            ::fcntl(f[1], F_SETFD, ::fcntl(f[1], F_GETFD) | FD_CLOEXEC);
            Reactor reactor;
            BandwidthProbe probe;
            std::atomic<int> done{0};
            PumpOptions opts;
            opts.spawn_source = [](int attempt, std::uint64_t off)
                    -> std::pair<pid_t, int> {
                int q[2];
                ::pipe(q);
                ::fcntl(q[0], F_SETFD, ::fcntl(q[0], F_GETFD) | FD_CLOEXEC);
                ::fcntl(q[1], F_SETFD, ::fcntl(q[1], F_GETFD) | FD_CLOEXEC);
                const pid_t pid = ::fork();
                if (pid == 0) {
                    ::dup2(q[1], STDOUT_FILENO);
                    if (attempt == 1)
                        ::execlp("sh", "sh", "-c",
                                 "head -c 10000 /dev/zero; exit 1", nullptr);
                    // attempt 2: so completa se o offset for EXATAMENTE 10000
                    const std::string cmd =
                        "tail -c +" + std::to_string(off + 1) +
                        " /dev/zero | head -c 5000";
                    ::execlp("sh", "sh", "-c", cmd.c_str(), nullptr);
                    _exit(127);
                }
                ::close(q[1]);
                return {pid, q[0]};
            };
            opts.max_source_retries = 1;
            const pid_t cons = ::fork();  // wc -c < f
            if (cons == 0) {
                ::dup2(f[0], STDIN_FILENO);
                ::execlp("wc", "wc", "-c", nullptr);
                _exit(127);
            }
            ::close(f[0]);
            auto task = pump_fds(reactor, f[1], probe, done, opts);
            done.wait(0);
            int stc = 0;
            ::waitpid(cons, &stc, 0);
            check("resume fino: 10000 + 5000 = 15000 bytes sem buraco",
                  done.load() - 1 == 0,
                  "code=" + std::to_string(done.load() - 1));
        } else {
            check("resume fino: 10000 + 5000 = 15000 bytes sem buraco", false,
                  "pipe falhou");
        }
    }
    // 11) STALL: fonte viva mas muda => kill e resume --------------------
    {
        int f[2];
        if (::pipe(f) == 0) {
            ::fcntl(f[0], F_SETFD, ::fcntl(f[0], F_GETFD) | FD_CLOEXEC);
            ::fcntl(f[1], F_SETFD, ::fcntl(f[1], F_GETFD) | FD_CLOEXEC);
            Reactor reactor;
            BandwidthProbe probe;
            std::atomic<int> done{0};
            PumpOptions opts;
            opts.spawn_source = [](int attempt, std::uint64_t)
                    -> std::pair<pid_t, int> {
                int q[2];
                ::pipe(q);
                ::fcntl(q[0], F_SETFD, ::fcntl(q[0], F_GETFD) | FD_CLOEXEC);
                ::fcntl(q[1], F_SETFD, ::fcntl(q[1], F_GETFD) | FD_CLOEXEC);
                const pid_t pid = ::fork();
                if (pid == 0) {
                    ::dup2(q[1], STDOUT_FILENO);
                    if (attempt == 1)
                        ::execlp("sh", "sh", "-c",
                                 "sleep 8; head -c 1000 /dev/zero", nullptr);
                    else
                        ::execlp("sh", "sh", "-c",
                                 "head -c 1000 /dev/zero", nullptr);
                    _exit(127);
                }
                ::close(q[1]);
                return {pid, q[0]};
            };
            opts.max_source_retries = 1;
            opts.stall_seconds = 3.0;
            const pid_t cons = ::fork();  // wc -c < f
            if (cons == 0) {
                ::dup2(f[0], STDIN_FILENO);
                ::execlp("wc", "wc", "-c", nullptr);
                _exit(127);
            }
            ::close(f[0]);
            auto task = pump_fds(reactor, f[1], probe, done, opts);
            done.wait(0);
            int stc = 0;
            ::waitpid(cons, &stc, 0);
            check("stall: fonte muda => kill + resume no offset",
                  done.load() - 1 == 0,
                  "code=" + std::to_string(done.load() - 1));
        } else {
            check("stall: fonte muda => kill + resume no offset", false,
                  "pipe falhou");
        }
    }
    return ok ? 0 : 1;
}

} // namespace saci
