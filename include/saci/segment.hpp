#pragma once
// Ontologia: SEGMENTO (unidade atomica em transito no pipeline).
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace saci {

// Move-only: um segmento nunca e copiado, apenas transferido entre estagios.
class Segment {
public:
    using Clock = std::chrono::steady_clock;

    Segment() = default;
    Segment(std::size_t cap) { reset(cap); }

    Segment(Segment&&) noexcept = default;
    Segment& operator=(Segment&&) noexcept = default;
    Segment(const Segment&) = delete;
    Segment& operator=(const Segment&) = delete;

    void reset(std::size_t cap) {
        data_ = std::make_unique<std::byte[]>(cap);
        cap_ = cap; len_ = 0;
    }

    const std::byte* data() const { return data_.get(); }
    std::byte* data() { return data_.get(); }
    std::size_t size() const { return len_; }
    std::size_t capacity() const { return cap_; }
    void set_size(std::size_t n) { len_ = n; }
    void clear() { len_ = 0; }

    void pts(std::chrono::milliseconds p) { pts_ = p; }
    std::chrono::milliseconds pts() const { return pts_; }

private:
    std::unique_ptr<std::byte[]> data_;
    std::size_t cap_ = 0;
    std::size_t len_ = 0;
    std::chrono::milliseconds pts_{0};
};

} // namespace saci
