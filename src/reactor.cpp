#include "saci/reactor.hpp"

#include <unistd.h>
#include <fcntl.h>

#include <cerrno>
#include <stdexcept>
#include <vector>

namespace saci {

Reactor::Reactor() {
    int fds[2];
    if (::pipe(fds) == -1) throw std::runtime_error("reactor: pipe falhou");
    wake_r_ = fds[0];
    wake_w_ = fds[1];
    // self-pipe NAO-bloqueante: o dreno para no EAGAIN em vez de dormir
    // no segundo read (bug real pego pelo selftest do pump)
    ::fcntl(wake_r_, F_SETFL, ::fcntl(wake_r_, F_GETFL) | O_NONBLOCK);
    ::fcntl(wake_w_, F_SETFL, ::fcntl(wake_w_, F_GETFL) | O_NONBLOCK);
    thread_ = std::jthread([this](std::stop_token st) { loop(st); });
}

Reactor::~Reactor() {
    // jthread dtor: request_stop() + join(); o poll acorda em <=100 ms.
    if (wake_w_ != -1) ::close(wake_w_);
    if (wake_r_ != -1) ::close(wake_r_);
}

void Reactor::add(FdWaiter* w) {
    {
        std::lock_guard lk(m_);
        waiters_[w->fd()] = w;
    }
    // Acorda o poll para recalcular o conjunto (level-triggered: nada se perde).
    const char c = 'x';
    ssize_t n = ::write(wake_w_, &c, 1);
    (void)n;
}

void Reactor::remove(int fd) {
    std::lock_guard lk(m_);
    waiters_.erase(fd);
}

void Reactor::loop(std::stop_token st) {
    std::vector<::pollfd> fds;
    while (!st.stop_requested()) {
        fds.clear();
        fds.push_back({wake_r_, POLLIN, 0});
        {
            std::lock_guard lk(m_);
            fds.reserve(waiters_.size() + 1);
            for (auto& [fd, w] : waiters_)
                fds.push_back({fd, w->events(), 0});
        }

        int r = ::poll(fds.data(), static_cast<::nfds_t>(fds.size()), 100);
        if (r == -1) {
            if (errno == EINTR) continue;
            break;
        }

        if (r > 0 && (fds[0].revents & POLLIN)) {  // self-pipe: so drena
            char buf[64];
            while (::read(wake_r_, buf, sizeof buf) > 0) {}
        }

        for (std::size_t i = 1; i < fds.size() && r > 0; ++i) {
            const short interest = fds[i].events | POLLERR | POLLHUP;
            if (!(fds[i].revents & interest)) continue;
            std::coroutine_handle<> h;
            {
                std::lock_guard lk(m_);
                auto it = waiters_.find(fds[i].fd);
                if (it == waiters_.end()) continue;
                FdWaiter* w = it->second;
                if (!w->try_complete()) continue;  // EAGAIN: segue registrado
                waiters_.erase(it);
                h = w->h_;
            }
            if (h) h.resume();  // FORA do lock: a coroutine pode re-registrar
        }
    }
}

bool Readable::try_complete() {
    const ssize_t n = ::read(fd_, buf_, len_);
    if (n >= 0) { result_ = n; return true; }
    if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) return false;
    result_ = -1;
    return true;
}

bool Writable::try_complete() {
    const ssize_t n = ::write(fd_, buf_, len_);
    if (n >= 0) { result_ = n; return true; }
    if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) return false;
    result_ = -1;
    return true;
}

} // namespace saci
