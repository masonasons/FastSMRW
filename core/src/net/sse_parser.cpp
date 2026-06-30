#include "fastsm/net/sse_parser.hpp"

namespace fastsm::net {

void SseParser::dispatch(const std::function<void(const SseEvent&)>& on_event) {
    if (has_field_) {
        // Strip the single trailing newline the data accumulation appended.
        if (!data_.empty() && data_.back() == '\n')
            data_.pop_back();
        on_event(SseEvent{event_, data_});
    }
    event_.clear();
    data_.clear();
    has_field_ = false;
}

void SseParser::feed(std::string_view bytes,
                     const std::function<void(const SseEvent&)>& on_event) {
    buf_.append(bytes);
    size_t start = 0;
    for (;;) {
        const size_t nl = buf_.find('\n', start);
        if (nl == std::string::npos)
            break;
        std::string line = buf_.substr(start, nl - start);
        start = nl + 1;
        if (!line.empty() && line.back() == '\r')
            line.pop_back();

        if (line.empty()) { // blank line terminates the event
            dispatch(on_event);
            continue;
        }
        if (line[0] == ':')
            continue; // comment / keep-alive

        std::string field, value;
        if (const size_t colon = line.find(':'); colon != std::string::npos) {
            field = line.substr(0, colon);
            value = line.substr(colon + 1);
            if (!value.empty() && value.front() == ' ')
                value.erase(value.begin()); // one optional leading space
        } else {
            field = line; // field with empty value
        }

        has_field_ = true;
        if (field == "event")
            event_ = value;
        else if (field == "data")
            data_ += value + "\n";
        // "id" / "retry" and unknown fields are ignored.
    }
    buf_.erase(0, start);
}

} // namespace fastsm::net
