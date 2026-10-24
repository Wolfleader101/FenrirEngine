#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "Math_fwd.hpp"

constexpr const float EPSILON = std::numeric_limits<float>::epsilon();

namespace Fenrir::Math
{
    // TODO in future might be worth seperating into seperate files?

    Math::Vec3 RoundToZero(const Math::Vec3& vec);

    /**
     * @brief Converts an angle from degrees to radians.
     *
     * @param deg Angle in degrees.
     * @return The angle in radians.
     */
    float DegToRad(float deg);

    /**
     * @brief Converts each angle in a Vec3 from degrees to radians.
     *
     * @param vec A Vec3 containing angles in degrees.
     * @return A Vec3 where each angle has been converted to radians.
     */
    Vec3 DegToRad(const Vec3& vec);

    /**
     * @brief Converts an angle from radians to degrees.
     *
     * @param rad Angle in radians.
     * @return The angle in degrees.
     */
    float RadToDeg(float rad);

    /**
     * @brief Converts each angle in a Vec3 from radians to degrees.
     *
     * @param vec A Vec3 containing angles in radians.
     * @return A Vec3 where each angle has been converted to degrees.
     */
    Vec3 RadToDeg(const Vec3& vec);

    /**
     * @brief Computes the dot product of two 2D vectors.
     *
     * @param a First 2D vector.
     * @param b Second 2D vector.
     * @return The dot product of the two vectors.
     */
    float Dot(const Vec2& a, const Vec2& b);

    /**
     * @brief Computes the dot product of two 3D vectors.
     *
     * @param a First 3D vector.
     * @param b Second 3D vector.
     * @return The dot product of the two vectors.
     */
    float Dot(const Vec3& a, const Vec3& b);

    /**
     * @brief Calculates the magnitude (or length) of a 2D vector.
     *
     * @param v A 2D vector.
     * @return The magnitude of the vector.
     */
    float Magnitude(const Vec2& v);

    /**
     * @brief Calculates the magnitude (or length) of a 3D vector.
     *
     * @param v A 3D vector.
     * @return The magnitude of the vector.
     */
    float Magnitude(const Vec3& v);

    /**
     * @brief Calculates the square of the magnitude of a 2D vector.
     *        This function is faster than Magnitude as it avoids the square root operation.
     *
     * @param v A 2D vector.
     * @return The square of the magnitude of the vector.
     */
    float MagnitudeSq(const Vec2& v);

    /**
     * @brief Calculates the square of the magnitude of a 3D vector.
     *        This function is faster than Magnitude as it avoids the square root operation.
     *
     * @param v A 3D vector.
     * @return The square of the magnitude of the vector.
     */
    float MagnitudeSq(const Vec3& v);

    /**
     * @brief Calculates the Euclidean distance between two points in space.
     *
     * @param p1 The first point.
     * @param p2 The second point.
     * @return The distance between the two points.
     */
    float Distance(const Point& p1, const Point& p2);

    float DistanceSq(const Point& p1, const Point& p2);

    /**
     * @brief Normalizes a 2D vector, making its length equal to 1.
     *
     * @param v A 2D vector. The vector is modified to be a unit vector.
     */
    void Normalize(Vec2& v);

    /**
     * @brief Normalizes a 3D vector, making its length equal to 1.
     *
     * @param v A 3D vector. The vector is modified to be a unit vector.
     */
    void Normalize(Vec3& v);

    /**
     * @brief Creates a new normalized 2D vector based on the input vector.
     *
     * @param v A 2D vector.
     * @return A new 2D vector that is a normalized version of the input vector.
     */
    Vec2 Normalized(const Vec2& v);

    /**
     * @brief Creates a new normalized 3D vector based on the input vector.
     *
     * @param v A 3D vector.
     * @return A new 3D vector that is a normalized version of the input vector.
     */
    Vec3 Normalized(const Vec3& v);

    /**
     * @brief Calculates the cross product of two 3D vectors.
     *
     * @param a The first 3D vector.
     * @param b The second 3D vector.
     * @return The cross product vector.
     */
    Vec3 Cross(const Vec3& a, const Vec3& b);

    /**
     * @brief Calculates the angle between two 2D vectors in radians.
     *
     * @param a The first 2D vector.
     * @param b The second 2D vector.
     * @return The angle in radians between the two vectors.
     */
    float Angle(const Vec2& a, const Vec2& b);

    float WrapAngle(float angle);
    Vec3 WrapEulerAngles(Vec3 euler);

    /**
     * @brief Calculates the angle between two 3D vectors in radians.
     *
     * @param a The first 3D vector.
     * @param b The second 3D vector.
     * @return The angle in radians between the two vectors.
     */
    float Angle(const Vec3& a, const Vec3& b);
    Quat AngleAxis(float angle, const Vec3& axis);

    /**
     * @brief Projects a 2D vector onto another 2D vector.
     *
     * @param length The vector to project.
     * @param direction The vector to project onto.
     * @return The projected vector.
     */
    Vec2 Project(const Vec2& length, const Vec2& direction);

    /**
     * @brief Projects a 3D vector onto another 3D vector.
     *
     * @param length The vector to project.
     * @param direction The vector to project onto.
     * @return The projected vector.
     */
    Vec3 Project(const Vec3& length, const Vec3& direction);

    /**
     * @brief Finds a vector that is perpendicular to both input 2D vectors.
     *
     * @param len The first 2D vector.
     * @param dir The second 2D vector.
     * @return The vector that is perpendicular to both input vectors.
     */
    Vec2 Perpendicular(const Vec2& len, const Vec2& dir);

    /**
     * @brief Finds a vector that is perpendicular to both input 3D vectors.
     *
     * @param len The first 3D vector.
     * @param dir The second 3D vector.
     * @return The vector that is perpendicular to both input vectors.
     */
    Vec3 Perpendicular(const Vec3& len, const Vec3& dir);

    /**
     * @brief Converts a quaternion to a 3D Euler angle vector.
     *
     * @param quat The quaternion to convert.
     * @return The 3D Euler angle vector.
     */
    Vec3 EulerFromQuat(const Quat& quat);

    Mat3 Cut(const Mat4& mat, int row, int col);

    Point MultiplyPoint(const Point& point, const Mat4& mat);

    /**
     * @brief Multiplies a 3D vector by a 4x4 matrix, typically used to transform the vector in 3D space.
     *
     * @param vec The 3D vector to be multiplied.
     * @param mat The 4x4 transformation matrix.
     * @return The transformed vector.
     */
    Vec3 MultiplyVector(const Vec3& vec, const Mat4& mat);

    /**
     * @brief Calculates the inverse of a 4x4 matrix.
     *
     * @param mat The 4x4 matrix to invert.
     * @return The inverted matrix.
     */
    Mat4 Inverse(const Mat4& mat);

    /**
     * @brief Calculates the inverse of a 3x3 matrix.
     *
     * @param mat The 3x3 matrix to invert.
     * @return The inverted matrix.
     */
    Mat3 Inverse(const Mat3& mat);

    /**
     * @brief Converts a quaternion to a 4x4 matrix.
     *
     * @param quaternion The quaternion to convert.
     * @return The resulting 4x4 matrix.
     */
    Mat4 Mat4Cast(const Quat& quaternion);

    Mat3 Mat3Cast(const Quat& quaternion);

    Mat3 Transpose(const Mat3& mat);

    Vec3 Rotate(const Quat& quat, const Vec3& vec);

    Quat Conjugate(const Quat& quat);

    /**
     * @brief Represents a line in 3D space defined by two points.
     */
    struct Line
    {
        Point start; ///< The starting point of the line.
        Point end;   ///< The ending point of the line.

        /**
         * @brief Default constructor, initializes a new line with unspecified start and end points.
         */
        inline Line()
        {
        }

        /**
         * @brief Constructor to initialize a line with specified start and end points.
         *
         * @param s The start point of the line.
         * @param e The end point of the line.
         */
        inline Line(const Point& s, const Point& e) : start(s), end(e)
        {
        }
    };

    /**
     * @brief Computes the length of a line.
     *
     * @param line The line whose length is to be computed.
     * @return The length of the line.
     */
    float Length(const Line& line);

    /**
     * @brief Computes the squared length of a line, which is faster than computing the length.
     *
     * @param line The line whose squared length is to be computed.
     * @return The squared length of the line.
     */
    float LengthSq(const Line& line);

    /**
     * @brief Represents a ray in 3D space defined by an origin point and a direction vector.
     */
    struct Ray
    {
        Point origin; ///< The origin point of the ray.
        Vec3 dir;     ///< The direction vector of the ray, should be normalized to represent a direction.

        /**
         * @brief Default constructor, initializes a new ray with a default direction pointing along the z-axis.
         */
        inline Ray() : dir(0.0f, 0.0f, 1.0f)
        {
        }

        /**
         * @brief Constructor to initialize a ray with specified origin point and direction vector.
         *
         * @param o The origin point of the ray.
         * @param d The direction vector of the ray.
         */
        inline Ray(const Point& o, const Vec3& d) : origin(o), dir(d)
        {
            NormalizeDirection();
        }

        /**
         * @brief Normalizes the direction vector of the ray.
         */
        inline void NormalizeDirection()
        {
            Normalize(dir);
        }
    };

    /**
     * @brief Creates a ray from two points. The first point is the origin and the second point helps determine the
     * direction.
     *
     * @param from The origin point of the ray.
     * @param to The point towards which the ray should point.
     * @return A ray initialized with the specified origin and direction.
     */
    Ray FromPoints(const Point& from, const Point& to);

    /**
     * @brief Perform linear interpolation between two values.
     *
     * @param a The starting value.
     * @param b The ending value.
     * @param t The interpolation parameter (between 0 and 1).
     * @return float The interpolated value.
     */
    float Lerp(float a, float b, float t);

    /**
     * @brief Calculate the fraction 't' (between 0 and 1) based on the given value.
     *
     * @param a The starting value.
     * @param b The ending value.
     * @param v The value to calculate the fraction for.
     * @return float The calculated fraction.
     */
    float InverseLerp(float a, float b, float v);

    /**
     * @brief Map a value from one input range to a corresponding value in an output range.
     *
     * @param iMin The minimum value of the input range.
     * @param iMax The maximum value of the input range.
     * @param oMin The minimum value of the output range.
     * @param oMax The maximum value of the output range.
     * @param v The value to remap.
     * @return float The remapped value.
     */
    float Remap(float iMin, float iMax, float oMin, float oMax, float v);

    /**
     * @brief Creates a perspective projection matrix.
     *
     * @param fovY The field of view in the y direction, in degrees.
     * @param aspect The aspect ratio, which is the width of the viewport divided by the height.
     * @param zNear The distance to the near clipping plane.
     * @param zFar The distance to the far clipping plane.
     * @return The perspective projection matrix.
     */
    Mat4 Perspective(float fovY, float aspect, float zNear, float zFar);

    /**
     * @brief Creates an orthographic projection matrix.
     *
     * @param left The coordinate for the left vertical clipping plane.
     * @param right The coordinate for the right vertical clipping plane.
     * @param bottom The coordinate for the bottom horizontal clipping plane.
     * @param top The coordinate for the top horizontal clipping plane.
     * @return The orthographic projection matrix.
     */
    Mat4 Orthographic(float left, float right, float bottom, float top);

    /**
     * @brief Creates a view matrix aimed at a target from a specified position and oriented according to a specified up
     * vector.
     *
     * @param eye The position of the camera.
     * @param center The point the camera is looking at.
     * @param up The up vector which defines the up direction for the camera.
     * @return The view matrix.
     */
    Mat4 LookAt(const Vec3& eye, const Vec3& center, const Vec3& up);

    Quat LookAt(const Vec3& direction, const Vec3& up);

    const float* AsArray(const Vec2& v);
    const float* AsArray(const Vec3& v);
    const float* AsArray(const Vec4& v);
    const float* AsArray(const Mat3& m);
    const float* AsArray(const Mat4& m);
    float* AsArray(Mat4& m);
    const float* AsArray(const Quat& q);

    Mat4 MakeMat4(const float* arr);

    void Decompose(const Mat4& mat, Vec3& pos, Quat& rotation, Vec3& scale);

    /**
     * @brief Calculates the cosine of the value.
     *
     * @param val The value in radians.
     * @return The cosine of val.
     */
    float Cos(float val);

    /**
     * @brief Translates a matrix by a vector.
     *
     * @param mat The matrix to be translated.
     * @param vec The translation vector.
     * @return The translated matrix.
     */
    Mat4 Translate(const Mat4& mat, const Vec3& vec);

    /**
     * @brief Scales a matrix by a vector.
     *
     * @param mat The matrix to be scaled.
     * @param vec The scaling vector.
     * @return The scaled matrix.
     */
    Mat4 Scale(const Mat4& mat, const Vec3& vec);

    /**
     * @brief Rotates a matrix around a specified axis.
     *
     * @param mat The matrix to be rotated.
     * @param angle The angle of rotation in degrees.
     * @param axis The axis of rotation.
     * @return The rotated matrix.
     */
    Mat4 Rotate(const Mat4& mat, float angle, const Vec3& axis);

    /**
     * @brief Creates a rotation matrix from euler angles
     *
     * @param euler in radians
     * @return Mat4
     */
    Mat4 EulerToMat4(const Vec3& euler);

    Quat Rotate(const Quat& quat, float angle, const Vec3& axis);

    /**
     * @brief Spherically interpolates between two quaternions.
     *
     * @param x The start quaternion.
     * @param y The end quaternion.
     * @param a The interpolation parameter between 0 and 1.
     * @return The interpolated quaternion.
     */
    Quat Slerp(const Quat& x, const Quat& y, float a);

    /**
     * @brief Normalizes a quaternion.
     *
     * @param x The quaternion to normalize.
     * @return The normalized quaternion.
     */
    Quat Normalized(const Quat& x);

    Quat Mat4ToQuat(const Mat4& mat);

    Mat4 QuatToMat4(const Quat& quat);

    Vec3 EulerFromMat4(const Mat4& mat);

    bool RayAABBIntersect(const Ray& ray, const AABB& aabb, const Mat4& transformMatrix);

    float CalculateDistance(const Fenrir::Math::Vec3& rayOrigin, const AABB& aabb,
                            const Fenrir::Math::Mat4& transformMatrix);

} // namespace Fenrir::Math
