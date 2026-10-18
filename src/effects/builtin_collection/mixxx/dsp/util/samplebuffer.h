// GlideMix: Mixxx's mixxx::SampleBuffer, reduced to what effects use.
#pragma once

#include <utility>

#include "util/sample.h"

namespace mixxx {
class SampleBuffer {
public:
    SampleBuffer() = default;
    explicit SampleBuffer(SINT size)
        : m_data(size > 0 ? SampleUtil::alloc(size) : nullptr), m_size(size > 0 ? size : 0) {}
    SampleBuffer(const SampleBuffer&) = delete;
    SampleBuffer& operator=(const SampleBuffer&) = delete;
    SampleBuffer(SampleBuffer&& that) noexcept
        : m_data(std::exchange(that.m_data, nullptr)), m_size(std::exchange(that.m_size, 0)) {}
    SampleBuffer& operator=(SampleBuffer&& that) noexcept
    {
        if (this != &that) {
            SampleUtil::free(m_data);
            m_data = std::exchange(that.m_data, nullptr);
            m_size = std::exchange(that.m_size, 0);
        }
        return *this;
    }
    ~SampleBuffer() { SampleUtil::free(m_data); }

    SINT size() const { return m_size; }
    CSAMPLE* data() { return m_data; }
    const CSAMPLE* data() const { return m_data; }
    CSAMPLE& operator[](SINT index) { return m_data[index]; }
    const CSAMPLE& operator[](SINT index) const { return m_data[index]; }
    void clear() { if (m_data) { SampleUtil::clear(m_data, m_size); } }

private:
    CSAMPLE* m_data = nullptr;
    SINT m_size = 0;
};
}
