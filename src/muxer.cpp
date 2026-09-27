#include "saci/muxer.hpp"

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>

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
    // ha EOF no leitor; e o pump depende de EOF do yt-dlp).
    if (read_fd != -1)  parent_closes.push_back(read_fd);
    if (write_fd != -1) parent_closes.push_back(write_fd);
    return pid;
}

void set_nonblock(int fd) {
    ::fcntl(fd, F_SETFL, ::fcntl(fd, F_GETFL) | O_NONBLOCK);
}

} // namespace

namespace saci {

MuxHandle::~MuxHandle() {
    for (int fd : fds_) ::close(fd);
    if (q_read_ != -1)  ::close(q_read_);
    if (f_write_ != -1) ::close(f_write_);
    for (int pid : pids_) { int s; ::waitpid(pid, &s, 0); }
}

// Pump (v0.3, roadmap item 3): saci vira o elo entre yt-dlp e ffmpeg.
// Mede a vazao real do video em janelas de 2 s e alimenta o BandwidthProbe
// (percentil 25 + Haar p/ quedas + FFT p/ periodicidade). Leitura e escrita
// nao-bloqueantes sob poll — o mesmo padrao do run_capture2.
int MuxHandle::pump() {
    set_nonblock(q_read_);
    set_nonblock(f_write_);

    constexpr double kWindowS = 2.0;
    std::uint64_t bytes = 0;
    auto t0 = std::chrono::steady_clock::now();

    std::string wbuf;          // dados lidos ainda nao escritos no ffmpeg
    std::size_t w_off = 0;
    bool q_eof = false, f_broken = false, ytd_reaped = false;
    int ytd_code = -1;
    std::array<char, 65536> buf{};

    while (!f_broken && (!q_eof || w_off < wbuf.size())) {
        if (!ytd_reaped) {  // reaper nao-bloqueante: nao segura o pump
            int st = 0;
            pid_t r = ::waitpid(ytdlp_pid_, &st, WNOHANG);
            if (r == ytdlp_pid_) {
                ytd_reaped = true;
                ytd_code = WIFEXITED(st) ? WEXITSTATUS(st) : -1;
            }
        }

        ::pollfd pfds[2];
        pfds[0] = {q_read_,  q_eof ? short(0) : short(POLLIN), 0};
        pfds[1] = {f_write_, w_off < wbuf.size() ? short(POLLOUT) : short(0), 0};
        int r = ::poll(pfds, 2, 500);  // timeout: estatistica + reaper
        if (r == -1 && errno != EINTR) break;

        if (r > 0 && (pfds[0].revents & (POLLIN | POLLHUP))) {
            for (;;) {  // drena o que houver
                ssize_t n = ::read(q_read_, buf.data(), buf.size());
                if (n > 0) {
                    wbuf.append(buf.data(), static_cast<std::size_t>(n));
                    bytes += static_cast<std::uint64_t>(n);
                } else if (n == 0) { q_eof = true; break; }
                else if (errno == EINTR) continue;
                else if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                else { q_eof = true; break; }
            }
            if (pfds[0].revents & POLLHUP) q_eof = true;
        }

        if (r > 0 && (pfds[1].revents & (POLLOUT | POLLERR | POLLHUP))) {
            while (w_off < wbuf.size()) {
                ssize_t n = ::write(f_write_, wbuf.data() + w_off, wbuf.size() - w_off);
                if (n > 0) { w_off += static_cast<std::size_t>(n); continue; }
                if (n == -1 && errno == EINTR) continue;
                if (n == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
                f_broken = true;   // ffmpeg morreu ou fechou a entrada
                break;
            }
            if (w_off == wbuf.size()) { wbuf.clear(); w_off = 0; }
        }

        // Janela de medicao: alimenta o probe e loga
        const auto now = std::chrono::steady_clock::now();
        const double dt = std::chrono::duration<double>(now - t0).count();
        if (dt >= kWindowS) {
            probe_.sample_window(bytes, dt);
            bytes = 0;
            t0 = now;
            std::cerr << "[saci] banda: janela "
                      << static_cast<long>(probe_.estimate_kbps() / 0.75 + 0.5)
                      << " kbps brutos, estimado p/ prox. degrau "
                      << static_cast<long>(probe_.estimate_kbps() + 0.5)
                      << ", drop=" << (probe_.drop_detected() ? "sim" : "nao")
                      << ", periodicidade=" << probe_.periodicity() << "\n";
        }
    }

    ::close(q_read_);  q_read_ = -1;
    if (!f_broken) { ::close(f_write_); f_write_ = -1; }
    else { ::close(f_write_); f_write_ = -1; ::kill(ytdlp_pid_, SIGKILL); }
    // Esgota o reaper do yt-dlp se ainda nao aconteceu (kill acima).
    if (!ytd_reaped) { int st = 0; ::waitpid(ytdlp_pid_, &st, 0); }

    // Fim natural = yt-dlp exit 0 E pump drenado. Resto e prematuro.
    premature_ = f_broken || ytd_code != 0;
    return f_broken ? 1 : 0;
}

int MuxHandle::wait_all() {
    int code = 0;
    if (q_read_ != -1 && f_write_ != -1 && ytdlp_pid_ != -1) {
        code = pump();
    } else {
        premature_ = true;
    }

    int ff_code = 0;
    for (int pid : pids_) {
        if (pid == ytdlp_pid_) continue;  // ja reapado no pump
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
    // q: yt-dlp -> pump | f: pump -> ffmpeg | p1: ffmpeg -> player
    int q[2], f[2], p1[2];
    if (::pipe(q) == -1 || ::pipe(f) == -1 || ::pipe(p1) == -1)
        throw std::runtime_error(std::strerror(errno));

    MuxHandle h;
    std::vector<int> parent_closes;

    // ffmpeg: stdin = f[0], stdout = p1[1]
    std::vector<std::string> fargv = {"ffmpeg", "-hide_banner", "-loglevel", "error",
        "-i", "pipe:0", "-i", sess.narration_path,
        "-map", "0:v", "-map", "1:a",
        "-c:v", "copy", "-c:a", "aac",
        "-shortest", "-f", "mp4", "-movflags", "frag_keyframe+empty_moov",
        "pipe:1"};
    h.ff_pid_ = spawn_pipe(fargv, f[0], p1[1], parent_closes);

    // yt-dlp: stdout = q[1] (o pump le q[0])
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

    // Pontas do pump ficam com o handle.
    h.q_read_ = q[0];
    h.f_write_ = f[1];

    return h;
}

} // namespace saci
