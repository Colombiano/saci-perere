#include "saci/muxer.hpp"

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <stdexcept>
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
                 std::vector<int>& out_fds) {
    pid_t pid = ::fork();
    if (pid == -1) throw std::runtime_error(std::strerror(errno));
    if (pid == 0) {
        if (read_fd != -1)  { ::dup2(read_fd, STDIN_FILENO); }
        if (write_fd != -1) { ::dup2(write_fd, STDOUT_FILENO); }
        exec_or_die(argv);
    }
    if (read_fd != -1)  out_fds.push_back(read_fd);
    if (write_fd != -1) out_fds.push_back(write_fd);
    return pid;
}

} // namespace

namespace saci {

MuxHandle::~MuxHandle() {
    for (int fd : fds_) ::close(fd);
    for (int pid : pids_) { int s; ::waitpid(pid, &s, 0); }
}

int MuxHandle::wait_all() {
    int code = 0;
    for (std::size_t i = 0; i < pids_.size(); ++i) {
        int status = 0;
        ::waitpid(pids_[i], &status, 0);
        if (code == 0 && WIFEXITED(status)) code = WEXITSTATUS(status);
    }
    pids_.clear();
    return code;
}

MuxHandle spawn_mux(const MuxSession& sess) {
    // yt-dlp -> pipe0 -> ffmpeg -> pipe1 -> player
    int p0[2], p1[2];
    if (::pipe(p0) == -1 || ::pipe(p1) == -1)
        throw std::runtime_error(std::strerror(errno));

    MuxHandle h;

    // ffmpeg: lado esquerdo p0 (stdin), lado direito p1 (stdout)
    std::vector<std::string> fargv = {"ffmpeg", "-hide_banner", "-loglevel", "error",
        "-i", "pipe:0", "-i", sess.narration_path,
        "-map", "0:v", "-map", "1:a",
        "-c:v", "copy", "-c:a", "aac",
        "-shortest", "-f", "mp4", "-movflags", "frag_keyframe+empty_moov",
        "pipe:1"};
    h.pids_.push_back(spawn_pipe(fargv, p0[0], p1[1], h.fds_));

    // yt-dlp escreve no lado de escrita de p0
    std::vector<std::string> src = sess.source_argv;
    src.insert(src.end(), {"-o", "-"});
    h.pids_.push_back(spawn_pipe(src, -1, p0[1], h.fds_));

    // player le do lado de leitura de p1
    h.pids_.push_back(spawn_pipe(sess.player_argv, p1[0], -1, h.fds_));

    return h;
}

} // namespace saci
