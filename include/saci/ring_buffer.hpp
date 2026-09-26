#pragma once
// Ontologia: ANEL DE BUFFER (backpressure para rede rural oscilante).
// FIFO circular de Segmentos move-only; cheio => push() retorna false
// (sinal de backpressure para o estagio produtor).
#include <array>
#include <cstddef>
#include <functional>
#include <optional>

namespace saci {

template <typename Seg, std::size_t N>
    requires std::movable<Seg>
class RingBuffer {
public:
    explicit RingBuffer(std::function<void(const Seg&)> on_evict = {})
        : on_evict_(std::move(on_evict)) {}

    RingBuffer(RingBuffer&&) noexcept = default;
    RingBuffer& operator=(RingBuffer&&) noexcept = default;
    RingBuffer(const RingBuffer&) = delete;
    RingBuffer& operator=(const RingBuffer&) = delete;

    // Retorna false se cheio (produtor deve esperar/reduzir degrau).
    bool push(Seg&& seg) {
        if (count_ == N) return false;
        slots_[tail_] = std::move(seg);
        tail_ = (tail_ + 1) % N;
        ++count_;
        return true;
    }

    std::optional<Seg> pop() {
        if (count_ == 0) return std::nullopt;
        Seg out = std::move(*slots_[head_]);
        slots_[head_].reset();
        head_ = (head_ + 1) % N;
        --count_;
        return out;
    }

    // Despeja o mais antigo (politica de sobrevivencia FIFO em disco cheio).
    void evict_oldest() {
        if (count_ == 0) return;
        if (on_evict_) on_evict_(*slots_[head_]);
        slots_[head_].reset();
        head_ = (head_ + 1) % N;
        --count_;
    }

    std::size_t size() const { return count_; }
    bool full() const { return count_ == N; }
    bool empty() const { return count_ == 0; }

private:
    std::array<std::optional<Seg>, N> slots_;
    std::size_t head_ = 0, tail_ = 0, count_ = 0;
    std::function<void(const Seg&)> on_evict_;
};

} // namespace saci
