#include "FenrirCamera/Camera.hpp"
namespace Fenrir
{
    Math::Mat4 Camera::GetViewMatrix()
    {
        return Math::LookAt(pos, pos + front, up);
    }

    Camera::Camera()
    {
        Update();
    }

    void Camera::Update()
    {
        Math::Vec3 tempFront;
        tempFront.x = cos(Fenrir::Math::DegToRad(yaw)) * cos(Fenrir::Math::DegToRad(pitch));
        tempFront.y = sin(Fenrir::Math::DegToRad(pitch));
        tempFront.z = sin(Fenrir::Math::DegToRad(yaw)) * cos(Fenrir::Math::DegToRad(pitch));
        front = Math::Normalized(tempFront);

        right = Math::Normalized(Math::Cross(front, worldUp));
        up = Math::Normalized(Math::Cross(right, front));
    }
} // namespace Fenrir