#include "FenrirMath/Math.hpp"
namespace Fenrir
{
    class Camera
    {
      public:
        Math::Vec3 pos = Math::Vec3(0.0f, 0.0f, 0.0f);
        Math::Vec3 front = Math::Vec3(0.0f, 0.0f, -1.0f);
        Math::Vec3 up = Math::Vec3(0.0f, 1.0f, 0.0f);
        Math::Vec3 right = Math::Vec3(1.0f, 0.0f, 0.0f);
        Fenrir::Math::Vec3 worldUp = Math::Vec3(0.0f, 1.0f, 0.0f);
        float yaw = -90.0f;
        float pitch = 0.0f;

        float fov = 70;

        Camera();

        Math::Mat4 GetViewMatrix();

        void Update();
    };
} // namespace Fenrir