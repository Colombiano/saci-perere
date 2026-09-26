#pragma once
// Task<T> coroutine minima, executada eager (sem event loop externo).
// Suficiente para orquestrar etapas I/O-bound sem std::thread.
#include <coroutine>
#include <exception>
#include <optional>
#include <utility>

namespace saci {

template <typename T>
struct Task {
    struct promise_type {
        std::optional<T> value;
        std::exception_ptr err;

        Task get_return_object() {
            return Task{std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        std::suspend_never initial_suspend() noexcept { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        void return_value(T v) { value = std::move(v); }
        void unhandled_exception() { err = std::current_exception(); }
    };

    using Handle = std::coroutine_handle<promise_type>;

    explicit Task(Handle h) : h_(h) {}
    Task(Task&& o) noexcept : h_(std::exchange(o.h_, nullptr)) {}
    Task& operator=(Task&& o) noexcept {
        if (this != &o) { if (h_) h_.destroy(); h_ = std::exchange(o.h_, nullptr); }
        return *this;
    }
    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;
    ~Task() { if (h_) h_.destroy(); }

    T get() {
        if (h_.promise().err) std::rethrow_exception(h_.promise().err);
        T v = std::move(*h_.promise().value);
        h_.destroy(); h_ = nullptr;
        return v;
    }

private:
    Handle h_;
};

template <>
struct Task<void> {
    struct promise_type {
        std::exception_ptr err;
        Task get_return_object() {
            return Task{std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        std::suspend_never initial_suspend() noexcept { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        void return_void() noexcept {}
        void unhandled_exception() { err = std::current_exception(); }
    };
    using Handle = std::coroutine_handle<promise_type>;
    explicit Task(Handle h) : h_(h) {}
    Task(Task&& o) noexcept : h_(std::exchange(o.h_, nullptr)) {}
    ~Task() { if (h_) h_.destroy(); }
    void get() {
        if (h_.promise().err) std::rethrow_exception(h_.promise().err);
        h_.destroy(); h_ = nullptr;
    }
private:
    Handle h_;
};

} // namespace saci
