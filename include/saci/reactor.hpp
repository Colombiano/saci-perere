#pragma once
// Ontologia: REATOR DE I/O (v0.4, roadmap item 5) — co_await real sobre pipes.
//
// Sem liburing, sem event loop externo: uma std::jthread roda poll(2);
// coroutines suspendem com co_await em prontidao de fd e sao retomadas pelo
// reator. Decisao registrada em ADR-002 (EXPLICACAO.md): poll proprio em vez
// de io_uring — zero dependencia, portavel, e o volume de fds do saci e 2.
//
// C++20 em acao: std::jthread + std::stop_token, std::atomic::wait/notify
// (espera sem busy-loop), concepts (FdWaiter), coroutines (co_await/co_return).
#include <coroutine>
#include <cstddef>
#include <mutex>
#include <poll.h>
#include <stop_token>
#include <thread>
#include <unordered_map>

namespace saci {

class Reactor;

// Interface do "interessado" em um fd: o reator conhece soh isto.
struct FdWaiter {
    virtual ~FdWaiter() = default;
    virtual int fd() const = 0;
    virtual short events() const = 0;
    // Uma tentativa nao-bloqueante; true => resolvido (resumir coroutine).
    virtual bool try_complete() = 0;
    std::coroutine_handle<> h_;
};

// Concept de documentacao: todo awaiter de fd do saci satisfaz isto.
template <typename W>
concept FdAwaiter = std::derived_from<W, FdWaiter>;

class Reactor {
public:
    Reactor();
    ~Reactor();
    Reactor(const Reactor&) = delete;
    Reactor& operator=(const Reactor&) = delete;

    // Registra interesse; retomacao acontece na thread do reator.
    // Thread-safe: await_suspend pode rodar em qualquer thread.
    void add(FdWaiter* w);
    void remove(int fd);

private:
    void loop(std::stop_token st);

    std::jthread thread_;
    std::mutex m_;
    std::unordered_map<int, FdWaiter*> waiters_;
    int wake_r_ = -1, wake_w_ = -1;  // self-pipe para acordar o poll
};

// co_await Readable{reactor, fd, buf, n} -> ssize_t lido (0 = EOF, -1 = erro)
class Readable : public FdWaiter {
public:
    Readable(Reactor& r, int fd, void* buf, std::size_t len)
        : reactor_(r), fd_(fd), buf_(buf), len_(len) {}

    bool await_ready() { return try_complete(); }
    void await_suspend(std::coroutine_handle<> h) { h_ = h; reactor_.add(this); }
    ssize_t await_resume() { return result_; }

    int fd() const override { return fd_; }
    short events() const override { return POLLIN; }
    bool try_complete() override;

private:
    Reactor& reactor_;
    int fd_;
    void* buf_;
    std::size_t len_;
    ssize_t result_ = 0;
};

// co_await Writable{reactor, fd, buf, n} -> ssize_t escrito (-1 = erro/EPIPE)
class Writable : public FdWaiter {
public:
    Writable(Reactor& r, int fd, const void* buf, std::size_t len)
        : reactor_(r), fd_(fd), buf_(buf), len_(len) {}

    bool await_ready() { return try_complete(); }
    void await_suspend(std::coroutine_handle<> h) { h_ = h; reactor_.add(this); }
    ssize_t await_resume() { return result_; }

    int fd() const override { return fd_; }
    short events() const override { return POLLOUT; }
    bool try_complete() override;

private:
    Reactor& reactor_;
    int fd_;
    const void* buf_;
    std::size_t len_;
    ssize_t result_ = 0;
};

} // namespace saci
