#include "saci/muxer.hpp"

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>

#include <array>
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

pid_t spawn_pipe(const std::vector<std::string>& argv, int read_fd, int write_fd,
                 std::vector<int>& parent_closes) {
    pid_t pid = ::fork();
    if (pid == -1) throw std::runtime_error(std::strerror(errno));
    if (pid == 0) {
        if (read_fd != -1)  { ::dup2(read_fd, STDIN_FILENO); }
        if (write_fd != -1) { ::dup2(write_fd, STDOUT_FILENO); }
        exec_or_die(argv);
    }
    // No PAI, essas pontas pertencem aos filhos: fecha logo para o EOF
    // fluir corretamente pela cadeia (parente segurando write-end = nunca
    // ha EOF no leitor; e a bomba depende de EOF do yt-dlp).
    if (read_fd != -1)  parent_closes.push_back(read_fd);
    if (write_fd != -1) parent_closes.push_back(write_fd);
    return pid;
}

void set_nonblock(int fd) {
    ::fcntl(fd, F_SETFL, ::fcntl(fd, F_GETFL) | O_NONBLOCK);
}

// CLOEXEC nas pontas dos pipes: sem isto, cada filho herda TODAS as pontas
// (fork copia a tabela de fds) e o EOF nunca chega — ex.: o player segurando
// f[1] impediria o EOF do ffmpeg para sempre. dup2 limpa CLOEXEC no alvo,
// entao o stdio dos filhos sobrevive ao exec.
void set_cloexec(int fd) {
    ::fcntl(fd, F_SETFD, ::fcntl(fd, F_GETFD) | FD_CLOEXEC);
}

} // namespace

namespace saci {

MuxHandle::~MuxHandle() {
    for (int fd : fds_) ::close(fd);
    if (q_read_ != -1)  ::close(q_read_);
    if (f_write_ != -1) ::close(f_write_);
    for (int pid : pids_) { int s; ::waitpid(pid, &s, 0); }
}

// A bomba como coroutine (v0.4): cada leitura/escrita e um co_await de
// prontidao — a maquina de estados implicta do poll() manual vira fluxo
// sequencial legivel. Mede a vazao real em janelas de 2 s e alimenta o
// BandwidthProbe (percentil 25 + Haar + FFT), como na v0.3.
Task<int> pump_fds(Reactor& reactor, int q_read, int f_write,
                   BandwidthProbe& probe, std::atomic<int>& done) {
    int code = 0;
    try {
        set_nonblock(q_read);
        set_nonblock(f_write);

        constexpr double kWindowS = 2.0;
        std::uint64_t bytes = 0;
        auto t0 = std::chrono::steady_clock::now();

        std::string wbuf;
        std::size_t w_off = 0;
        bool q_eof = false, f_broken = false;
        std::array<char, 65536> buf{};

        while (!q_eof || w_off < wbuf.size()) {
            if (w_off < wbuf.size()) {
                const ssize_t n = co_await Writable{reactor, f_write,
                                                    wbuf.data() + w_off,
                                                    wbuf.size() - w_off};
                if (n > 0) { w_off += static_cast<std::size_t>(n); }
                else       { f_broken = true; code = 1; }
                if (w_off == wbuf.size()) { wbuf.clear(); w_off = 0; }
            } else {
                const ssize_t n = co_await Readable{reactor, q_read,
                                                    buf.data(), buf.size()};
                if (n > 0) {
                    wbuf.append(buf.data(), static_cast<std::size_t>(n));
                    bytes += static_cast<std::uint64_t>(n);
                } else {
                    q_eof = true;
                    if (n < 0) code = 1;  // erro de leitura = prematuro
                }
            }

            // Janela de medicao (nao-bloqueante): alimenta o probe e loga
            const auto now = std::chrono::steady_clock::now();
            const double dt = std::chrono::duration<double>(now - t0).count();
            if (dt >= kWindowS) {
                probe.sample_window(bytes, dt);
                bytes = 0;
                t0 = now;
                std::cerr << "[saci] banda: janela "
                          << static_cast<long>(probe.estimate_kbps() / 0.75 + 0.5)
                          << " kbps brutos, estimado p/ prox. degrau "
                          << static_cast<long>(probe.estimate_kbps() + 0.5)
                          << ", drop=" << (probe.drop_detected() ? "sim" : "nao")
                          << ", periodicidade=" << probe.periodicity() << "\n";
            }
        }

        ::close(q_read);
        if (!f_broken) { ::close(f_write); }
        else           { ::close(f_write); }  // EPIPE: ffmpeg ja nao escuta
    } catch (...) {
        code = 2;
    }
    done.store(code + 1);  // 0 = rodando; code+1 sinaliza fim
    done.notify_all();     // C++20: acorda quem espera sem busy-loop
    co_return code;
}

int MuxHandle::wait_all() {
    int code = 0;
    if (q_read_ != -1 && f_write_ != -1 && ytdlp_pid_ != -1) {
        Reactor reactor;
        std::atomic<int> done{0};
        {
            auto task = pump_fds(reactor, q_read_, f_write_, probe_, done);
            q_read_ = f_write_ = -1;  // ownership dos fds vai com a coroutine
        }
        done.wait(0);  // C++20 atomic::wait — dorme ate a bomba sinalizar
        code = done.load() - 1;

        int st = 0;
        ::waitpid(ytdlp_pid_, &st, 0);
        const int ytd = WIFEXITED(st) ? WEXITSTATUS(st) : -1;
        premature_ = (code != 0) || (ytd != 0);
    } else {
        premature_ = true;
    }

    int ff_code = 0;
    for (int pid : pids_) {
        if (pid == ytdlp_pid_) continue;  // ja reapado acima
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
    // q: yt-dlp -> bomba | f: bomba -> ffmpeg | p1: ffmpeg -> player
    int q[2], f[2], p1[2];
    if (::pipe(q) == -1 || ::pipe(f) == -1 || ::pipe(p1) == -1)
        throw std::runtime_error(std::strerror(errno));

    MuxHandle h;
    std::vector<int> parent_closes;

    // CLOEXEC em todas as pontas: filhos nao herdam as pontas da bomba
    // (dup2 dos filhos limpa CLOEXEC apenas no stdio deles).
    for (int fd : {q[0], q[1], f[0], f[1], p1[0], p1[1]}) set_cloexec(fd);

    // ffmpeg: stdin = f[0], stdout = p1[1]
    std::vector<std::string> fargv = {"ffmpeg", "-hide_banner", "-loglevel", "error",
        "-i", "pipe:0", "-i", sess.narration_path,
        "-map", "0:v", "-map", "1:a",
        "-c:v", "copy", "-c:a", "aac",
        "-shortest", "-f", "mp4", "-movflags", "frag_keyframe+empty_moov",
        "pipe:1"};
    h.ff_pid_ = spawn_pipe(fargv, f[0], p1[1], parent_closes);

    // yt-dlp: stdout = q[1] (a bomba le q[0])
    std::vector<std::string> src = sess.source_argv;
    src.insert(src.end(), {"-o", "-"});
    h.ytdlp_pid_ = spawn_pipe(src, -1, q[1], parent_closes);

    // player: stdin = p1[0]
    int player_pid = spawn_pipe(sess.player_argv, p1[0], -1, parent_closes);

    // Pai nao segura nenhuma ponta dos pipes da cadeia (EOF correto).
    for (int fd : parent_closes) ::close(fd);

    h.pids_.push_back(player_pid);
    h.pids_.push_back(h.ff_pid_);
    h.pids_.push_back(h.ytdlp_pid_);

    // Pontas da bomba ficam com o handle.
    h.q_read_ = q[0];
    h.f_write_ = f[1];

    return h;
}

} // namespace saci
