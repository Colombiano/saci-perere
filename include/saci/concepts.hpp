#pragma once
// Ontologia: CONCEITOS (constraints de tipo do pipeline).
// Todo buffer de midia no sistema e move-only, com data()/size()/clear().
#include <concepts>
#include <cstddef>
#include <optional>

namespace saci {

template <typename T>
concept MovableBuffer = std::movable<T> && requires(T t) {
    { t.data() } -> std::convertible_to<const std::byte*>;
    { t.size() } -> std::convertible_to<std::size_t>;
    { t.clear() } -> std::same_as<void>;
};

template <typename S, typename Seg>
concept SegmentSource = requires(S s) {
    { s.next() } -> std::same_as<std::optional<Seg>>;
};

} // namespace saci
