#include "saci/proc.hpp"

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>

#include <array>
#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <vector>

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

} // namespace saci
