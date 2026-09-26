#include "saci/subtitle.hpp"

#include <charconv>
#include <sstream>
#include <stdexcept>

namespace saci {

std::int64_t srt_timestamp_to_ms(const std::string& ts) {
    // "HH:MM:SS,mmm"
    if (ts.size() < 12) throw std::runtime_error("timestamp SRT invalido: " + ts);
    auto num = [&](std::size_t p, std::size_t len) {
        std::int64_t v = 0;
        std::from_chars(ts.data() + p, ts.data() + p + len, v);
        return v;
    };
    return num(0, 2) * 3600000 + num(3, 2) * 60000 + num(6, 2) * 1000 + num(9, 3);
}

std::vector<TranscriptSegment> parse_srt(const std::string& content) {
    std::vector<TranscriptSegment> out;
    std::istringstream in(content);
    std::string line;
    TranscriptSegment cur;
    enum { WantIndex, WantTimes, WantText } state = WantIndex;

    auto flush = [&] {
        if (!cur.text.empty()) {
            cur.index = out.size();
            out.push_back(std::move(cur));
            cur = TranscriptSegment{};
        }
    };

    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) { flush(); state = WantIndex; continue; }

        switch (state) {
        case WantIndex:
            state = WantTimes;  // numero do bloco; ignoramos (usamos ordem)
            break;
        case WantTimes: {
            auto arrow = line.find("-->");
            if (arrow == std::string::npos) break;
            std::string a = line.substr(0, arrow);
            std::string b = line.substr(arrow + 3);
            auto trim = [](std::string s) {
                auto i = s.find_first_not_of(" \t");
                auto j = s.find_last_not_of(" \t");
                return i == std::string::npos ? "" : s.substr(i, j - i + 1);
            };
            cur.start_ms = srt_timestamp_to_ms(trim(a));
            cur.end_ms = srt_timestamp_to_ms(trim(b).substr(0, 12));
            state = WantText;
            break;
        }
        case WantText:
            if (!cur.text.empty()) cur.text += ' ';
            cur.text += line;
            break;
        }
    }
    flush();
    return out;
}

} // namespace saci
