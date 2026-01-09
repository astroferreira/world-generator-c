#pragma once

#include <cmath>

namespace worldgen {

template<typename T>
struct Vec2 {
    T x, y;

    Vec2() : x(0), y(0) {}
    Vec2(T x, T y) : x(x), y(y) {}

    Vec2 operator+(const Vec2& other) const { return {x + other.x, y + other.y}; }
    Vec2 operator-(const Vec2& other) const { return {x - other.x, y - other.y}; }
    Vec2 operator*(T scalar) const { return {x * scalar, y * scalar}; }
    Vec2 operator/(T scalar) const { return {x / scalar, y / scalar}; }

    Vec2& operator+=(const Vec2& other) { x += other.x; y += other.y; return *this; }
    Vec2& operator-=(const Vec2& other) { x -= other.x; y -= other.y; return *this; }
    Vec2& operator*=(T scalar) { x *= scalar; y *= scalar; return *this; }
    Vec2& operator/=(T scalar) { x /= scalar; y /= scalar; return *this; }

    T length() const { return std::sqrt(x * x + y * y); }
    T lengthSquared() const { return x * x + y * y; }

    Vec2 normalized() const {
        T len = length();
        if (len > 0) return {x / len, y / len};
        return {0, 0};
    }

    void normalize() {
        T len = length();
        if (len > 0) { x /= len; y /= len; }
    }

    static T dot(const Vec2& a, const Vec2& b) {
        return a.x * b.x + a.y * b.y;
    }
};

using Vec2f = Vec2<float>;
using Vec2i = Vec2<int>;

} // namespace worldgen
