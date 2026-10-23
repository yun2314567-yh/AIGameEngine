#include "TinyEngine/Math.h"

#if defined(TINYENGINE_WITH_GLM)
#include <glm/glm.hpp>
#include <glm/gtx/quaternion.hpp>
#endif

namespace TinyEngine {
namespace {

#if defined(TINYENGINE_WITH_GLM)
glm::mat4 toGlm(const Mat4& value) {
    glm::mat4 result(0.0f);
    for (std::uint32_t column = 0; column < 4; ++column) {
        for (std::uint32_t row = 0; row < 4; ++row) result[column][row] = value(column, row);
    }
    return result;
}

Mat4 fromGlm(const glm::mat4& value) {
    Mat4 result;
    for (std::uint32_t column = 0; column < 4; ++column) {
        for (std::uint32_t row = 0; row < 4; ++row) result(column, row) = value[column][row];
    }
    return result;
}

glm::quat toGlm(const Quat& value) {
    return {value.w, value.x, value.y, value.z};
}
#endif

} // namespace

Quat Quat::fromEulerRadians(const Vec3& euler) {
    const float cx = std::cos(euler.x * 0.5f);
    const float sx = std::sin(euler.x * 0.5f);
    const float cy = std::cos(euler.y * 0.5f);
    const float sy = std::sin(euler.y * 0.5f);
    const float cz = std::cos(euler.z * 0.5f);
    const float sz = std::sin(euler.z * 0.5f);
    return {sx * cy * cz - cx * sy * sz,
            cx * sy * cz + sx * cy * sz,
            cx * cy * sz - sx * sy * cz,
            cx * cy * cz + sx * sy * sz};
}

Mat4 Mat4::identity() {
    Mat4 result;
    result(0, 0) = result(1, 1) = result(2, 2) = result(3, 3) = 1.0f;
    return result;
}

Mat4 Mat4::translation(const Vec3& value) {
    Mat4 result = identity();
    result(3, 0) = value.x;
    result(3, 1) = value.y;
    result(3, 2) = value.z;
    return result;
}

Mat4 Mat4::scale(const Vec3& value) {
    Mat4 result{};
    result(0, 0) = value.x;
    result(1, 1) = value.y;
    result(2, 2) = value.z;
    result(3, 3) = 1.0f;
    return result;
}

Mat4 Mat4::rotation(const Quat& input) {
    const Quat value = input.normalized();
#if defined(TINYENGINE_WITH_GLM)
    return fromGlm(glm::mat4_cast(toGlm(value)));
#else
    Mat4 result = identity();
    result(0, 0) = 1.0f - 2.0f * (value.y * value.y + value.z * value.z);
    result(0, 1) = 2.0f * (value.x * value.y + value.z * value.w);
    result(0, 2) = 2.0f * (value.x * value.z - value.y * value.w);
    result(1, 0) = 2.0f * (value.x * value.y - value.z * value.w);
    result(1, 1) = 1.0f - 2.0f * (value.x * value.x + value.z * value.z);
    result(1, 2) = 2.0f * (value.y * value.z + value.x * value.w);
    result(2, 0) = 2.0f * (value.x * value.z + value.y * value.w);
    result(2, 1) = 2.0f * (value.y * value.z - value.x * value.w);
    result(2, 2) = 1.0f - 2.0f * (value.x * value.x + value.y * value.y);
    return result;
#endif
}

Mat4 Mat4::trs(const Vec3& translationValue, const Quat& rotationValue, const Vec3& scaleValue) {
    return translation(translationValue) * rotation(rotationValue) * scale(scaleValue);
}

Vec3 Mat4::transformPoint(const Vec3& value) const {
#if defined(TINYENGINE_WITH_GLM)
    const glm::vec4 transformed = toGlm(*this) * glm::vec4(value.x, value.y, value.z, 1.0f);
    return transformed.w != 0.0f
        ? Vec3{transformed.x / transformed.w, transformed.y / transformed.w, transformed.z / transformed.w}
        : Vec3{transformed.x, transformed.y, transformed.z};
#else
    const float w = values[3] * value.x + values[7] * value.y + values[11] * value.z + values[15];
    const float xValue = values[0] * value.x + values[4] * value.y + values[8] * value.z + values[12];
    const float yValue = values[1] * value.x + values[5] * value.y + values[9] * value.z + values[13];
    const float zValue = values[2] * value.x + values[6] * value.y + values[10] * value.z + values[14];
    return w != 0.0f ? Vec3{xValue / w, yValue / w, zValue / w} : Vec3{xValue, yValue, zValue};
#endif
}

Mat4 operator*(const Mat4& lhs, const Mat4& rhs) {
#if defined(TINYENGINE_WITH_GLM)
    return fromGlm(toGlm(lhs) * toGlm(rhs));
#else
    Mat4 result{};
    for (std::uint32_t column = 0; column < 4; ++column) {
        for (std::uint32_t row = 0; row < 4; ++row) {
            for (std::uint32_t index = 0; index < 4; ++index) {
                result(column, row) += lhs(index, row) * rhs(column, index);
            }
        }
    }
    return result;
#endif
}

bool nearlyEqual(const Vec3& lhs, const Vec3& rhs, float epsilon) {
    return std::fabs(lhs.x - rhs.x) <= epsilon && std::fabs(lhs.y - rhs.y) <= epsilon && std::fabs(lhs.z - rhs.z) <= epsilon;
}

bool nearlyEqual(const Quat& lhs, const Quat& rhs, float epsilon) {
    return std::fabs(lhs.x - rhs.x) <= epsilon && std::fabs(lhs.y - rhs.y) <= epsilon && std::fabs(lhs.z - rhs.z) <= epsilon && std::fabs(lhs.w - rhs.w) <= epsilon;
}

} // namespace TinyEngine
