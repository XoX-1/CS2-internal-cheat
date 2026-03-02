#pragma once

#include <cmath>

struct Vector2 {
    float x, y;

    Vector2() : x(0), y(0) {}
    Vector2(float x, float y) : x(x), y(y) {}

    Vector2 operator+(const Vector2& v) const { return Vector2(x + v.x, y + v.y); }
    Vector2 operator-(const Vector2& v) const { return Vector2(x - v.x, y - v.y); }
    Vector2 operator*(float f) const { return Vector2(x * f, y * f); }
};

struct Vector3 {
    float x, y, z;

    Vector3() : x(0), y(0), z(0) {}
    Vector3(float x, float y, float z) : x(x), y(y), z(z) {}

    Vector3 operator+(const Vector3& v) const { return Vector3(x + v.x, y + v.y, z + v.z); }
    Vector3 operator-(const Vector3& v) const { return Vector3(x - v.x, y - v.y, z - v.z); }
    Vector3 operator*(float f) const { return Vector3(x * f, y * f, z * f); }
    Vector3 operator/(float f) const { return Vector3(x / f, y / f, z / f); }

    Vector3& operator+=(const Vector3& v) { x += v.x; y += v.y; z += v.z; return *this; }
    Vector3& operator-=(const Vector3& v) { x -= v.x; y -= v.y; z -= v.z; return *this; }
    Vector3& operator*=(float f) { x *= f; y *= f; z *= f; return *this; }

    float Length() const { return sqrtf(x * x + y * y + z * z); }

    void Clamp() {
        if (x > 89.0f) x = 89.0f;
        if (x < -89.0f) x = -89.0f;
        while (y > 180.0f) y -= 360.0f;
        while (y < -180.0f) y += 360.0f;
        z = 0.0f;
    }

    static Vector3 CalculateAngle(const Vector3& src, const Vector3& dst) {
        Vector3 delta = dst - src;
        float dist = sqrtf(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);
        Vector3 angles;
        angles.x = -asinf(delta.z / dist) * (180.0f / 3.14159265f);
        angles.y = atan2f(delta.y, delta.x) * (180.0f / 3.14159265f);
        angles.z = 0.0f;
        return angles;
    }
};

using QAngle = Vector3;

// ViewMatrix is column-major: matrix[col][row]
using view_matrix_t = float[4][4];

inline bool WorldToScreen(const Vector3& world, Vector2& screen, const view_matrix_t& matrix, int screenWidth, int screenHeight) {
    float w = matrix[3][0] * world.x + matrix[3][1] * world.y + matrix[3][2] * world.z + matrix[3][3];

    if (w < 0.001f)
        return false;

    float x = matrix[0][0] * world.x + matrix[0][1] * world.y + matrix[0][2] * world.z + matrix[0][3];
    float y = matrix[1][0] * world.x + matrix[1][1] * world.y + matrix[1][2] * world.z + matrix[1][3];

    float invW = 1.0f / w;
    x *= invW;
    y *= invW;

    screen.x = (screenWidth / 2.0f) + (x * screenWidth / 2.0f);
    screen.y = (screenHeight / 2.0f) - (y * screenHeight / 2.0f);

    return true;
}
