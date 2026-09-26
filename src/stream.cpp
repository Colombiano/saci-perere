#include "saci/stream.hpp"

namespace saci {

std::string yt_dlp_format_selector(int max_height) {
    // video-only (acodec=none): o audio original sera substituido pela
    // narracao TTS no mux. Escolhe o maior degrau <= max_height.
    return "bv*[height<=" + std::to_string(max_height) + "][acodec=none]"
           "/bv*[acodec=none]/bv*";
}

} // namespace saci
