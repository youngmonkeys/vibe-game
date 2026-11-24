#pragma once

#include <cmath>

namespace vibe {

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

inline Vec3 operator+(Vec3 lhs, Vec3 rhs) { return Vec3{lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z}; }
inline Vec3 operator-(Vec3 lhs, Vec3 rhs) { return Vec3{lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z}; }
inline Vec3 operator*(Vec3 v, float s) { return Vec3{v.x * s, v.y * s, v.z * s}; }
inline Vec3 lerp(Vec3 from, Vec3 to, float alpha) { return from + (to - from) * alpha; }

struct Vec4 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 1.0f;
};

struct Quat {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 1.0f;

    static Quat fromAxisAngle(Vec3 axis, float radians) {
        const float length = std::sqrt(axis.x * axis.x + axis.y * axis.y + axis.z * axis.z);
        if (length == 0.0f) {
            return Quat{};
        }
        const float s = std::sin(radians * 0.5f) / length;
        return Quat{axis.x * s, axis.y * s, axis.z * s, std::cos(radians * 0.5f)};
    }

    static Quat aroundZ(float radians) { return Quat{0.0f, 0.0f, std::sin(radians * 0.5f), std::cos(radians * 0.5f)}; }

    static Quat fromEuler(float pitchX, float yawY, float rollZ) {
        return fromAxisAngle(Vec3{0.0f, 1.0f, 0.0f}, yawY) * fromAxisAngle(Vec3{1.0f, 0.0f, 0.0f}, pitchX) *
               fromAxisAngle(Vec3{0.0f, 0.0f, 1.0f}, rollZ);
    }

    Quat operator*(const Quat& r) const {
        return Quat{w * r.x + x * r.w + y * r.z - z * r.y, w * r.y - x * r.z + y * r.w + z * r.x,
                    w * r.z + x * r.y - y * r.x + z * r.w, w * r.w - x * r.x - y * r.y - z * r.z};
    }

    Quat normalized() const {
        const float length = std::sqrt(x * x + y * y + z * z + w * w);
        return length == 0.0f ? Quat{} : Quat{x / length, y / length, z / length, w / length};
    }

    static Quat nlerp(const Quat& from, const Quat& to, float alpha) {
        const float sign = (from.x * to.x + from.y * to.y + from.z * to.z + from.w * to.w) < 0.0f ? -1.0f : 1.0f;
        return Quat{from.x + (to.x * sign - from.x) * alpha, from.y + (to.y * sign - from.y) * alpha,
                    from.z + (to.z * sign - from.z) * alpha, from.w + (to.w * sign - from.w) * alpha}
            .normalized();
    }
};

struct Mat4 {
    float m[16] = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};

    float at(int row, int column) const { return m[column * 4 + row]; }

    static Mat4 compose(Vec3 position, Quat rotation, Vec3 scale) {
        const Quat q = rotation.normalized();
        const float xx = q.x * q.x;
        const float yy = q.y * q.y;
        const float zz = q.z * q.z;
        const float xy = q.x * q.y;
        const float xz = q.x * q.z;
        const float yz = q.y * q.z;
        const float wx = q.w * q.x;
        const float wy = q.w * q.y;
        const float wz = q.w * q.z;
        Mat4 out;
        out.m[0] = (1.0f - 2.0f * (yy + zz)) * scale.x;
        out.m[1] = 2.0f * (xy + wz) * scale.x;
        out.m[2] = 2.0f * (xz - wy) * scale.x;
        out.m[4] = 2.0f * (xy - wz) * scale.y;
        out.m[5] = (1.0f - 2.0f * (xx + zz)) * scale.y;
        out.m[6] = 2.0f * (yz + wx) * scale.y;
        out.m[8] = 2.0f * (xz + wy) * scale.z;
        out.m[9] = 2.0f * (yz - wx) * scale.z;
        out.m[10] = (1.0f - 2.0f * (xx + yy)) * scale.z;
        out.m[12] = position.x;
        out.m[13] = position.y;
        out.m[14] = position.z;
        return out;
    }

    Mat4 operator*(const Mat4& r) const {
        Mat4 out;
        for (int column = 0; column < 4; ++column) {
            for (int row = 0; row < 4; ++row) {
                float sum = 0.0f;
                for (int k = 0; k < 4; ++k) {
                    sum += at(row, k) * r.at(k, column);
                }
                out.m[column * 4 + row] = sum;
            }
        }
        return out;
    }

    static Mat4 perspective(float fovYRadians, float aspect, float nearPlane, float farPlane) {
        const float f = 1.0f / std::tan(fovYRadians * 0.5f);
        Mat4 out;
        out.m[0] = f / aspect;
        out.m[5] = f;
        out.m[10] = farPlane / (nearPlane - farPlane);
        out.m[11] = -1.0f;
        out.m[14] = nearPlane * farPlane / (nearPlane - farPlane);
        out.m[15] = 0.0f;
        return out;
    }

    static Mat4 orthographic(float halfWidth, float halfHeight, float nearPlane, float farPlane) {
        Mat4 out;
        out.m[0] = 1.0f / halfWidth;
        out.m[5] = 1.0f / halfHeight;
        out.m[10] = 1.0f / (nearPlane - farPlane);
        out.m[14] = nearPlane / (nearPlane - farPlane);
        return out;
    }

    Vec4 transform(Vec4 p) const {
        return Vec4{m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12] * p.w, m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13] * p.w,
                    m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14] * p.w, m[3] * p.x + m[7] * p.y + m[11] * p.z + m[15] * p.w};
    }

    Vec3 transformPoint(Vec3 p) const {
        return Vec3{m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12], m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13],
                    m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14]};
    }

    Mat4 affineInverse() const {
        const float a = m[0], b = m[4], c = m[8];
        const float d = m[1], e = m[5], f = m[9];
        const float g = m[2], h = m[6], i = m[10];
        const float c00 = e * i - f * h;
        const float c01 = f * g - d * i;
        const float c02 = d * h - e * g;
        const float det = a * c00 + b * c01 + c * c02;
        if (det == 0.0f) {
            return Mat4{};
        }
        const float inv = 1.0f / det;
        Mat4 out;
        out.m[0] = c00 * inv;
        out.m[1] = c01 * inv;
        out.m[2] = c02 * inv;
        out.m[4] = (c * h - b * i) * inv;
        out.m[5] = (a * i - c * g) * inv;
        out.m[6] = (b * g - a * h) * inv;
        out.m[8] = (b * f - c * e) * inv;
        out.m[9] = (c * d - a * f) * inv;
        out.m[10] = (a * e - b * d) * inv;
        const Vec3 t{m[12], m[13], m[14]};
        out.m[12] = -(out.m[0] * t.x + out.m[4] * t.y + out.m[8] * t.z);
        out.m[13] = -(out.m[1] * t.x + out.m[5] * t.y + out.m[9] * t.z);
        out.m[14] = -(out.m[2] * t.x + out.m[6] * t.y + out.m[10] * t.z);
        return out;
    }

    Vec3 translation() const { return Vec3{m[12], m[13], m[14]}; }
    float rotationZ() const { return std::atan2(m[1], m[0]); }
    float planarScaleX() const { return std::hypot(m[0], m[1]); }
    float planarScaleY() const {
        const float sx = planarScaleX();
        return sx == 0.0f ? 0.0f : (m[0] * m[5] - m[1] * m[4]) / sx;
    }
};

struct Transform {
    Vec3 position;
    Quat rotation;
    Vec3 scale{1.0f, 1.0f, 1.0f};

    Mat4 matrix() const { return Mat4::compose(position, rotation, scale); }

    static Transform lerp(const Transform& from, const Transform& to, float alpha) {
        return Transform{vibe::lerp(from.position, to.position, alpha), Quat::nlerp(from.rotation, to.rotation, alpha),
                         vibe::lerp(from.scale, to.scale, alpha)};
    }
};

} // namespace vibe
