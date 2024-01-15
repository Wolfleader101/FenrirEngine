#include "FenrirMath/Math.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/norm.hpp>

namespace Fenrir::Math
{
    Math::Vec3 RoundToZero(const Math::Vec3& vec)
    {
        Math::Vec3 result = vec;
        if (std::abs(result.x) < EPSILON)
            result.x = 0.0f;
        if (std::abs(result.y) < EPSILON)
            result.y = 0.0f;
        if (std::abs(result.z) < EPSILON)
            result.z = 0.0f;
        return result;
    }

    float Dot(const Vec2& a, const Vec2& b)
    {
        return glm::dot(a, b);
    }

    float Dot(const Vec3& a, const Vec3& b)
    {
        return glm::dot(a, b);
    }

    float DegToRad(const float deg)
    {
        return glm::radians(deg);
    }

    Vec3 DegToRad(const Vec3& vec)
    {
        return glm::radians(vec);
    }

    float RadToDeg(const float rad)
    {
        return glm::degrees(rad);
    }

    Vec3 RadToDeg(const Vec3& vec)
    {
        return glm::degrees(vec);
    }

    float Magnitude(const Vec2& v)
    {
        return glm::length(v);
    }

    float Magnitude(const Vec3& v)
    {
        return glm::length(v);
    }

    float MagnitudeSq(const Vec2& v)
    {
        return glm::length2(v);
    }

    float MagnitudeSq(const Vec3& v)
    {
        return glm::length2(v);
    }

    float Distance(const Point& p1, const Point& p2)
    {
        return glm::distance(p1, p2);
    }

    float DistanceSq(const Point& p1, const Point& p2)
    {
        return glm::distance2(p1, p2);
    }

    void Normalize(Vec2& v)
    {
        v = glm::normalize(v);
    }

    void Normalize(Vec3& v)
    {
        v = glm::normalize(v);
    }

    Vec2 Normalized(const Vec2& v)
    {
        return glm::normalize(v);
    }

    Vec3 Normalized(const Vec3& v)
    {
        return glm::normalize(v);
    }

    Vec3 Cross(const Vec3& a, const Vec3& b)
    {
        return glm::cross(a, b);
    }

    float Angle(const Vec2& a, const Vec2& b)
    {
        float m = sqrtf(MagnitudeSq(a) * MagnitudeSq(b));
        return acos(Dot(a, b) / m);
    }

    float Angle(const Vec3& a, const Vec3& b)
    {
        float m = sqrtf(MagnitudeSq(a) * MagnitudeSq(b));
        return acos(Dot(a, b) / m);
    }

    Quat AngleAxis(float angle, const Vec3& axis)
    {
        return glm::angleAxis(angle, axis);
    }

    Vec2 Project(const Vec2& length, const Vec2& direction)
    {
        float dot = Dot(length, direction);
        float magSq = MagnitudeSq(direction);

        return direction * (dot / magSq);
    }

    Vec3 Project(const Vec3& length, const Vec3& direction)
    {
        float dot = Dot(length, direction);
        float magSq = MagnitudeSq(direction);
        return direction * (dot / magSq);
    }

    Vec2 Perpendicular(const Vec2& len, const Vec2& dir)
    {
        return len - Project(len, dir);
    }

    Vec3 Perpendicular(const Vec3& len, const Vec3& dir)
    {
        return len - Project(len, dir);
    }

    float Length(const Line& line)
    {
        return Magnitude(line.start - line.end);
    }

    float LengthSq(const Line& line)
    {
        return MagnitudeSq(line.start - line.end);
    }

    Ray FromPoints(const Point& from, const Point& to)
    {
        return Ray(from, Normalized(to - from));
    }

    Mat4 Mat4Cast(const Quat& quaternion)
    {
        return glm::mat4_cast(quaternion);
    }

    Mat3 Mat3Cast(const Quat& quat)
    {
        return glm::mat3_cast(quat);
    }

    Mat3 Transpose(const Mat3& mat)
    {
        return glm::transpose(mat);
    }

    Point MultiplyPoint(const Point& point, const Mat4& mat)
    {
        Math::Vec4 tempPoint(point, 1.0f);
        tempPoint = mat * tempPoint;
        return Point(tempPoint);
    }

    Vec3 MultiplyVector(const Vec3& vec, const Mat4& mat)
    {
        Math::Vec4 temp(vec, 0.0f);
        temp = mat * temp;
        return Vec3(temp);
    }

    Mat3 Cut(const Mat4& mat, const int row, const int col)
    {
        Mat3 res;
        int targetRow = 0;
        for (int i = 0; i < 4; ++i)
        {
            if (i == row)
                continue;
            int targetCol = 0;
            for (int j = 0; j < 4; ++j)
            {
                if (j == col)
                    continue;
                res[targetRow][targetCol] = mat[i][j];
                ++targetCol;
            }
            ++targetRow;
        }
        return res;
    }

    Mat4 Inverse(const Mat4& mat)
    {
        return glm::inverse(mat);
    }

    Mat3 Inverse(const Mat3& mat)
    {
        return glm::inverse(mat);
    }

    float Lerp(const float a, const float b, const float t)
    {
        return (1.0f - t) * a + b * t;
    }

    /// return a fraction 't' (between 0 and 1) based on the given value
    float InverseLerp(const float a, const float b, const float v)
    {
        return (v - a) / (b - a);
    }

    /// takes a value within a given input range into a given output range
    float Remap(const float iMin, const float iMax, const float oMin, const float oMax, const float v)
    {
        const float t = InverseLerp(iMin, iMax, v);

        return Lerp(oMin, oMax, t);
    }

    Mat4 Perspective(const float fovY, const float aspect, const float zNear, const float zFar)
    {
        return glm::perspective(fovY, aspect, zNear, zFar);
    }

    Mat4 Orthographic(const float left, const float right, const float bottom, const float top)
    {
        return glm::ortho(left, right, bottom, top);
    }

    Mat4 LookAt(const Vec3& eye, const Vec3& center, const Vec3& up)
    {
        return glm::lookAt(eye, center, up);
    }

    Quat LookAt(const Vec3& direction, const Vec3& up)
    {
        return glm::quatLookAt(direction, up);
    }

    const float* AsArray(const Vec2& v)
    {
        return glm::value_ptr(v);
    }

    const float* AsArray(const Vec3& v)
    {
        return glm::value_ptr(v);
    }

    const float* AsArray(const Vec4& v)
    {
        return glm::value_ptr(v);
    }

    const float* AsArray(const Mat3& m)
    {
        return glm::value_ptr(m);
    }

    const float* AsArray(const Mat4& m)
    {
        return glm::value_ptr(m);
    }

    float* AsArray(Mat4& m)
    {
        return glm::value_ptr(m);
    }

    const float* AsArray(const Quat& q)
    {
        return glm::value_ptr(q);
    }

    Mat4 MakeMat4(const float* arr)
    {
        return glm::make_mat4(arr);
    }

    void Decompose(const Mat4& mat, Vec3& pos, Quat& rotation, Vec3& scale)
    {
        glm::vec3 skew;
        glm::vec4 perspective;
        glm::decompose(mat, scale, rotation, pos, skew, perspective);
    }

    float Cos(const float val)
    {
        return glm::cos(val);
    }

    Mat4 Translate(const Mat4& mat, const Vec3& vec)
    {
        return glm::translate(mat, vec);
    }

    Mat4 Scale(const Mat4& mat, const Vec3& vec)
    {
        return glm::scale(mat, vec);
    }

    Vec3 Rotate(const Quat& quat, const Vec3& vec)
    {
        return quat * vec;
    }

    Mat4 Rotate(const Mat4& mat, const float angle, const Vec3& axis)
    {
        return glm::rotate(mat, angle, axis);
    }

    Quat Rotate(const Quat& quat, const float angle, const Vec3& axis)
    {
        return glm::rotate(quat, angle, axis);
    }

    Quat Slerp(const Quat& x, const Quat& y, const float a)
    {
        return glm::slerp(x, y, a);
    }

    Quat Normalized(const Quat& x)
    {
        return glm::normalize(x);
    }

    float WrapAngle(float angle)
    {
        while (angle > 180)
            angle -= 360;
        while (angle < -180)
            angle += 360;
        return angle;
    }

    Vec3 WrapEulerAngles(Vec3 euler)
    {
        return Vec3(WrapAngle(euler.x), WrapAngle(euler.y), WrapAngle(euler.z));
    }

    Vec3 EulerFromQuat(const Quat& quat)
    {

        return glm::eulerAngles(glm::normalize(quat));
    }

    Quat Conjugate(const Quat& quat)
    {
        return glm::conjugate(quat);
    }

    bool RayAABBIntersect(const Ray& ray, const AABB& aabb, const Mat4& transformMatrix)
    {
        Vec3 minWorld = MultiplyPoint(aabb.min, transformMatrix);
        Vec3 maxWorld = MultiplyPoint(aabb.max, transformMatrix);

        Vec3 invDir;
        invDir.x = 1.0f / (ray.dir.x == 0.0f ? EPSILON : ray.dir.x);
        invDir.y = 1.0f / (ray.dir.y == 0.0f ? EPSILON : ray.dir.y);
        invDir.z = 1.0f / (ray.dir.z == 0.0f ? EPSILON : ray.dir.z);

        float t1 = (minWorld.x - ray.origin.x) * invDir.x;
        float t2 = (maxWorld.x - ray.origin.x) * invDir.x;
        float t3 = (minWorld.y - ray.origin.y) * invDir.y;
        float t4 = (maxWorld.y - ray.origin.y) * invDir.y;
        float t5 = (minWorld.z - ray.origin.z) * invDir.z;
        float t6 = (maxWorld.z - ray.origin.z) * invDir.z;

        float tmin = std::max(std::max(std::min(t1, t2), std::min(t3, t4)), std::min(t5, t6));
        float tmax = std::min(std::min(std::max(t1, t2), std::max(t3, t4)), std::max(t5, t6));

        if (tmax < 0 || tmin > tmax)
            return false;

        return true;
    }

    float CalculateDistance(const Fenrir::Math::Vec3& rayOrigin, const AABB& aabb,
                            const Fenrir::Math::Mat4& transformMatrix)
    {
        Vec3 minWorld = MultiplyPoint(aabb.min, transformMatrix);
        Vec3 maxWorld = MultiplyPoint(aabb.max, transformMatrix);

        Vec3 clampedOrigin = glm::clamp(rayOrigin, minWorld, maxWorld);

        return Distance(clampedOrigin, rayOrigin);
    }

    Mat4 EulerToMat4(const Vec3& vec)
    {
        return glm::eulerAngleXYZ(vec.x, vec.y, vec.z);
    }

    Quat Mat4ToQuat(const Mat4& mat)
    {
        return glm::quat_cast(mat);
    }

    Mat4 QuatToMat4(const Quat& quat)
    {
        return glm::mat4_cast(quat);
    }

    Vec3 EulerFromMat4(const Mat4& mat)
    {
        return glm::eulerAngles(glm::quat_cast(mat));
    }
} // namespace Fenrir::Math