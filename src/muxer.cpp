#include "saci/muxer.hpp"

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>

#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void exec_or_die(const std::vector<std::string>& argv) {
    std::vector<std::string> mut = argv;
    std::vector<char*> ptrs;
    for (auto& s : mut) ptrs.push_back(s.data());
    ptrs.push_back(nullptr);
    ::execvp(ptrs[0], ptrs.data());
    _exit(127);
}

void set_cloexec(int fd) {
    ::fcntl(fd, F_SETFD, ::fcntl(fd, F_GETFD) | FD_CLOEXEC);
}

void set_nonblock(int fd) {
    ::fcntl(fd, F_SETFL, ::fcntl(fd, F_GETFL) | O_NONBLOCK);
}

} // namespace

namespace saci {

MuxHandle::~MuxHandle() {
    for (int fd : fds_) ::close(fd);
    if (f_write_ != -1) ::close(f_write_);
    for (int pid : pids_) { int s; ::waitpid(pid, &s, 0); }
}

// A bomba como coroutine (v0.4) com RESUME FINO da fonte (v0.5):
// loop externo de (re)spawn; tentativa 1 vem do spawner (yt-dlp) e as
// retomadas usam `curl --range <offset>-` na URL direta. O offset e
// EXATO: bytes ja escritos no ffmpeg + pendencias do buffer — o wbuf e
// despejado antes da nova fonte ler, entao o stream segue sem duplicar
// nem furar. ffmpeg/player nunca reiniciam: o video "pausa" e volta.
Task<int> pump_fds(Reactor& reactor, int f_write, BandwidthProbe& probe,
                   std::atomic<int>& done, PumpOptions opts,
                   std::uint64_t start_offset) {
    int code = 0;
    try {
        set_nonblock(f_write);

        constexpr double kWindowS = 2.0;
        std::uint64_t delivered = start_offset;  // bytes escritos no ffmpeg
        std::string wbuf;
        std::size_t w_off = 0;

        for (int attempt = 1;; ++attempt) {
            const std::uint64_t src_offset =
                delivered + (wbuf.size() - w_off);  // proximo byte da fonte
            auto [src_pid, q] = opts.spawn_source(attempt, src_offset);
            set_nonblock(q);

            std::uint64_t bytes = 0;
            double quiet = 0;
            auto t0 = std::chrono::steady_clock::now();
            bool q_eof = false, f_broken = false, stall = false;
            int src_code = -1;
            std::array<char, 65536> buf{};

            while (!q_eof || w_off < wbuf.size()) {
                if (w_off < wbuf.size()) {
                    const ssize_t n = co_await Writable{reactor, f_write,
                                                        wbuf.data() + w_off,
                                                        wbuf.size() - w_off};
                    if (n > 0) {
                        w_off += static_cast<std::size_t>(n);
                        delivered += static_cast<std::uint64_t>(n);
                    } else {  // EPIPE: ffmpeg morreu
                        f_broken = true;
                        code = 1;
                        q_eof = true;
                        wbuf.clear();
                        w_off = 0;
                    }
                    if (w_off == wbuf.size()) { wbuf.clear(); w_off = 0; }
                } else {
                    const ssize_t n = co_await Readable{reactor, q,
                                                        buf.data(), buf.size()};
                    if (n > 0) {
                        wbuf.append(buf.data(), static_cast<std::size_t>(n));
                        bytes += static_cast<std::uint64_t>(n);
                        quiet = 0;
                    } else {
                        q_eof = true;
                    }
                }

                const auto now = std::chrono::steady_clock::now();
                const double dt = std::chrono::duration<double>(now - t0).count();
                if (dt >= kWindowS) {
                    probe.sample_window(bytes, dt);
                    bytes = 0;
                    t0 = now;
                    quiet += dt;
                    if (quiet >= opts.stall_seconds && !q_eof) {
                        // fonte viva mas MUDA: kill e resume no offset exato
                        ::kill(src_pid, SIGKILL);
                        stall = true;
                        q_eof = true;
                        wbuf.clear();  // nao entregues: a nova fonte reenvia
                        w_off = 0;
                    }
                    std::cerr << "[saci] banda: janela "
                              << static_cast<long>(probe.estimate_kbps() / 0.75 + 0.5)
                              << " kbps brutos, estimado p/ prox. degrau "
                              << static_cast<long>(probe.estimate_kbps() + 0.5)
                              << ", drop=" << (probe.drop_detected() ? "sim" : "nao")
                              << ", periodicidade=" << probe.periodicity() << "\n";
                }
            }
            ::close(q);

            int st = 0;
            ::waitpid(src_pid, &st, 0);
            src_code = WIFEXITED(st) ? WEXITSTATUS(st) : -1;

            if (f_broken) break;  // ffmpeg morreu: fora do nosso alcance
            if (stall) {
                if (attempt > opts.max_source_retries) { code = 1; break; }
                std::cerr << "[saci] fonte muda ha " << opts.stall_seconds
                          << "s; resume a partir do byte " << delivered << "\n";
                continue;
            }
            if (src_code == 0) break;  // fim natural do video
            if (attempt > opts.max_source_retries) { code = 1; break; }
            std::cerr << "[saci] fonte caiu (exit " << src_code
                      << "); resume HTTP Range a partir do byte " << delivered
                      << " (tentativa " << attempt << ")\n";
        }

        ::close(f_write);
    } catch (...) {
        code = 2;
    }
    done.store(code + 1);  // 0 = rodando; code+1 sinaliza fim
    done.notify_all();     // C++20: acorda quem espera sem busy-loop
    co_return code;
}

int MuxHandle::wait_all() {
    int code = 0;
    if (f_write_ != -1 && ff_pid_ != -1) {
        PumpOptions opts;
        opts.spawn_source = [this](int attempt, std::uint64_t off)
                -> std::pair<pid_t, int> {
            int q[2];
            if (::pipe(q) == -1) throw std::runtime_error(std::strerror(errno));
            set_cloexec(q[0]);
            set_cloexec(q[1]);
            std::vector<std::string> argv;
            if (attempt == 1 || direct_url_.empty()) {
                argv = src_argv_;
                argv.insert(argv.end(), {"-o", "-"});
            } else {
                // resume fino: continua do byte exato na URL direta
                argv = {"curl", "-sS", "-L", "--fail", "--range",
                        std::to_string(off) + "-", direct_url_};
            }
            const pid_t pid = ::fork();
            if (pid == 0) {
                ::dup2(q[1], STDOUT_FILENO);
                exec_or_die(argv);
            }
            ::close(q[1]);
            return {pid, q[0]};
        };
        opts.max_source_retries = direct_url_.empty() ? 0 : source_retries_;
        opts.stall_seconds = 6.0;

        Reactor reactor;
        std::atomic<int> done{0};
        {
            auto task = pump_fds(reactor, f_write_, probe_, done, opts);
            f_write_ = -1;  // ownership do fd vai com a coroutine
        }
        done.wait(0);  // C++20 atomic::wait — dorme ate a bomba sinalizar
        code = done.load() - 1;
        premature_ = (code != 0);
    } else {
        premature_ = true;
    }

    int ff_code = 0;
    for (int pid : pids_) {
        int status = 0;
        ::waitpid(pid, &status, 0);
        if (pid == ff_pid_) ff_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        // player: exit livre (usuario fechar o mpv nao e falha nossa)
    }
    pids_.clear();
    if (code == 0) code = ff_code;
    return code;
}

MuxHandle spawn_mux(const MuxSession& sess) {
    // f: bomba -> ffmpeg | p1: ffmpeg -> player | (fonte: a bomba cria)
    int f[2], p1[2];
    if (::pipe(f) == -1 || ::pipe(p1) == -1)
        throw std::runtime_error(std::strerror(errno));

    MuxHandle h;
    std::vector<int> parent_closes;
    for (int fd : {f[0], f[1], p1[0], p1[1]}) set_cloexec(fd);

    // ffmpeg: stdin = f[0], stdout = p1[1]
    std::vector<std::string> fargv = {"ffmpeg", "-hide_banner", "-loglevel", "error",
        "-i", "pipe:0", "-i", sess.narration_path,
        "-map", "0:v", "-map", "1:a",
        "-c:v", "copy", "-c:a", "aac",
        "-shortest", "-f", "mp4", "-movflags", "frag_keyframe+empty_moov",
        "pipe:1"};
    pid_t ff = ::fork();
    if (ff == -1) throw std::runtime_error(std::strerror(errno));
    if (ff == 0) {
        ::dup2(f[0], STDIN_FILENO);
        ::dup2(p1[1], STDOUT_FILENO);
        exec_or_die(fargv);
    }
    parent_closes.push_back(f[0]);
    parent_closes.push_back(p1[1]);

    // player: stdin = p1[0]
    pid_t player = ::fork();
    if (player == -1) throw std::runtime_error(std::strerror(errno));
    if (player == 0) {
        ::dup2(p1[0], STDIN_FILENO);
        exec_or_die(sess.player_argv);
    }
    parent_closes.push_back(p1[0]);

    for (int fd : parent_closes) ::close(fd);

    h.ff_pid_ = ff;
    h.pids_.push_back(player);
    h.pids_.push_back(ff);
    h.f_write_ = f[1];

    // stash da sessao para o spawner de fontes (resume fino)
    h.src_argv_ = sess.source_argv;
    h.direct_url_ = sess.direct_url;
    h.source_retries_ = sess.source_retries;

    return h;
}

} // namespace saci
