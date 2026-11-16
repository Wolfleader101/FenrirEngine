#include <cmath>

#include "FenrirPainter/Paint.hpp"

namespace Fenrir
{
    Transform2D Transform2D::Translation(Math::Vec2 offset)
    {
        return {{1.0f, 0.0f}, {0.0f, 1.0f}, offset};
    }

    Transform2D Transform2D::Scaling(Math::Vec2 factors)
    {
        return {{factors.x, 0.0f}, {0.0f, factors.y}, {0.0f, 0.0f}};
    }

    Transform2D Transform2D::Rotation(float radians)
    {
        const float c = std::cos(radians);
        const float s = std::sin(radians);
        // With Y down, the X axis turning towards +Y is clockwise on screen.
        return {{c, s}, {-s, c}, {0.0f, 0.0f}};
    }

    Math::Vec2 Transform2D::Apply(Math::Vec2 point) const
    {
        return x * point.x + y * point.y + origin;
    }

    Math::Vec2 Transform2D::ApplyVector(Math::Vec2 vector) const
    {
        return x * vector.x + y * vector.y;
    }

    Transform2D Transform2D::Inverse() const
    {
        const float det = x.x * y.y - y.x * x.y;
        if (std::abs(det) < 1e-12f)
        {
            return {};
        }
        Transform2D inverse;
        inverse.x = Math::Vec2(y.y, -x.y) / det;
        inverse.y = Math::Vec2(-y.x, x.x) / det;
        inverse.origin = -(inverse.x * origin.x + inverse.y * origin.y);
        return inverse;
    }

    float Transform2D::AreaScale() const
    {
        return std::abs(x.x * y.y - y.x * x.y);
    }

    bool Transform2D::IsAxisAligned() const
    {
        return x.y == 0.0f && y.x == 0.0f && x.x > 0.0f && y.y > 0.0f;
    }

    Transform2D operator*(const Transform2D& a, const Transform2D& b)
    {
        return {a.ApplyVector(b.x), a.ApplyVector(b.y), a.Apply(b.origin)};
    }
} // namespace Fenrir
