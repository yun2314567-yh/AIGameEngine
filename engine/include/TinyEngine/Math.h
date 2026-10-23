#pragma once

#include <array>
#include <cmath>
#include <cstdint>

namespace TinyEngine {

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    constexpr Vec3() = default;
    constexpr Vec3(float xValue, float yValue, float zValue) : x(xValue), y(yValue), z(zValue) {}

    constexpr Vec3 operator+(const Vec3& rhs) const { return {x + rhs.x, y + rhs.y, z + rhs.z}; }
    constexpr Vec3 operator-(const Vec3& rhs) const { return {x - rhs.x, y - rhs.y, z - rhs.z}; }
    constexpr Vec3 operator-() const { return {-x, -y, -z}; }
    constexpr Vec3 operator*(float scalar) const { return {x * scalar, y * scalar, z * scalar}; }
    constexpr Vec3 operator/(float scalar) const { return {x / scalar, y / scalar, z / scalar}; }
    constexpr Vec3& operator+=(const Vec3& rhs) { x += rhs.x; y += rhs.y; z += rhs.z; return *this; }
    constexpr Vec3& operator-=(const Vec3& rhs) { x -= rhs.x; y -= rhs.y; z -= rhs.z; return *this; }
    constexpr Vec3& operator*=(float scalar) { x *= scalar; y *= scalar; z *= scalar; return *this; }

    [[nodiscard]] float lengthSquared() const { return x * x + y * y + z * z; }
    [[nodiscard]] float length() const { return std::sqrt(lengthSquared()); }
    [[nodiscard]] Vec3 normalized() const {
        const float magnitude = length();
        return magnitude > 0.0f ? (*this / magnitude) : Vec3{};
    }
};

constexpr Vec3 operator*(float scalar, const Vec3& value) { return value * scalar; }
constexpr float dot(const Vec3& lhs, const Vec3& rhs) { return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z; }
constexpr Vec3 cross(const Vec3& lhs, const Vec3& rhs) {
    return {lhs.y * rhs.z - lhs.z * rhs.y, lhs.z * rhs.x - lhs.x * rhs.z, lhs.x * rhs.y - lhs.y * rhs.x};
}

struct Quat {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 1.0f;

    constexpr Quat() = default;
    constexpr Quat(float xValue, float yValue, float zValue, float wValue) : x(xValue), y(yValue), z(zValue), w(wValue) {}
    friend constexpr Quat operator*(const Quat& lhs, const Quat& rhs);

    [[nodiscard]] Quat normalized() const {
        const float magnitude = std::sqrt(x * x + y * y + z * z + w * w);
        return magnitude > 0.0f ? Quat{x / magnitude, y / magnitude, z / magnitude, w / magnitude} : Quat{};
    }
    [[nodiscard]] Quat conjugate() const { return {-x, -y, -z, w}; }
    [[nodiscard]] Vec3 rotate(const Vec3& value) const {
        const Quat vector{value.x, value.y, value.z, 0.0f};
        const Quat result = (*this) * vector * conjugate();
        return {result.x, result.y, result.z};
    }
    static Quat fromEulerRadians(const Vec3& euler);
};

constexpr Quat operator*(const Quat& lhs, const Quat& rhs) {
    return {lhs.w * rhs.x + lhs.x * rhs.w + lhs.y * rhs.z - lhs.z * rhs.y,
            lhs.w * rhs.y - lhs.x * rhs.z + lhs.y * rhs.w + lhs.z * rhs.x,
            lhs.w * rhs.z + lhs.x * rhs.y - lhs.y * rhs.x + lhs.z * rhs.w,
            lhs.w * rhs.w - lhs.x * rhs.x - lhs.y * rhs.y - lhs.z * rhs.z};
}

struct Mat4 {
    // Column-major storage, matching common graphics APIs.
    std::array<float, 16> values{};

    constexpr float& operator()(std::uint32_t column, std::uint32_t row) { return values[column * 4 + row]; }
    constexpr float operator()(std::uint32_t column, std::uint32_t row) const { return values[column * 4 + row]; }
    static Mat4 identity();
    static Mat4 translation(const Vec3& value);
    static Mat4 scale(const Vec3& value);
    static Mat4 rotation(const Quat& value);
    static Mat4 trs(const Vec3& translation, const Quat& rotation, const Vec3& scale);
    [[nodiscard]] Vec3 transformPoint(const Vec3& value) const;
};

Mat4 operator*(const Mat4& lhs, const Mat4& rhs);
bool nearlyEqual(const Vec3& lhs, const Vec3& rhs, float epsilon = 1.0e-5f);
bool nearlyEqual(const Quat& lhs, const Quat& rhs, float epsilon = 1.0e-5f);

} // namespace TinyEngine
