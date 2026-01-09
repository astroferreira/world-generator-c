#pragma once

#include "utils/Vec2.hpp"
#include <vector>
#include <cstddef>

namespace worldgen {

class Heightmap {
public:
    using ValueType = float;

    Heightmap(size_t width, size_t height);

    // Accessors
    ValueType get(size_t x, size_t y) const;
    ValueType& at(size_t x, size_t y);
    ValueType getInterpolated(float x, float y) const;
    Vec2f getGradient(size_t x, size_t y) const;

    // Modification
    void set(size_t x, size_t y, ValueType value);
    void add(size_t x, size_t y, ValueType delta);
    void fill(ValueType value);
    void normalize();

    // Properties
    size_t width() const { return m_width; }
    size_t height() const { return m_height; }
    size_t size() const { return m_data.size(); }

    // Raw access
    ValueType* data() { return m_data.data(); }
    const ValueType* data() const { return m_data.data(); }

    // Statistics
    ValueType min() const;
    ValueType max() const;
    ValueType average() const;

private:
    size_t m_width;
    size_t m_height;
    std::vector<ValueType> m_data;

    size_t index(size_t x, size_t y) const { return y * m_width + x; }
};

} // namespace worldgen
