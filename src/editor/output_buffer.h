#pragma once

#include <cstddef>
#include <deque>
#include <string>

namespace sk {

// Una linea del panel Output: nivel (0 info, 1 aviso, 2 error) y texto.
struct OutputLine {
    int level = 0;
    std::string text;
};

// Buffer circular del panel Output. Al superar la capacidad (5.000 por
// defecto) se descarta la linea mas antigua. Logica pura (sin Win32) para
// poder probarla en motorsk_model.
class OutputBuffer {
public:
    static constexpr size_t kDefaultCapacity = 5000;

    explicit OutputBuffer(size_t capacity = kDefaultCapacity)
        : capacity_(capacity == 0 ? 1 : capacity) {}

    void add(int level, const std::string& text) {
        lines_.push_back(OutputLine{level, text});
        while (lines_.size() > capacity_) {
            lines_.pop_front();
            ++dropped_;
        }
    }

    void clear() {
        lines_.clear();
        dropped_ = 0;
    }

    size_t size() const { return lines_.size(); }
    size_t capacity() const { return capacity_; }
    // Lineas descartadas por desbordamiento desde el ultimo clear().
    size_t dropped() const { return dropped_; }

    const OutputLine& at(size_t index) const { return lines_[index]; }
    const std::deque<OutputLine>& lines() const { return lines_; }

private:
    std::deque<OutputLine> lines_;
    size_t capacity_;
    size_t dropped_ = 0;
};

} // namespace sk
