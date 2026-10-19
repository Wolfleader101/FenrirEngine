#pragma once

// DemoCamera: where a scene wants the DemoApp's camera, as plain data, so a scene's own camera code (C++ or C#) can
// drive the one camera without knowing it. DemoCameras is the system that copies it into the camera.

#include <cstdint>

#include "FenrirApp/App.hpp"
#include "FenrirCamera/Camera.hpp"
#include "FenrirECS/Entity.hpp"
#include "FenrirMath/Math.hpp"

/** @brief The camera's pose a scene asks for: copied into the DemoApp's camera every frame while Active is set. */
struct DemoCamera
{
    Fenrir::Math::Vec3 Position = Fenrir::Math::Vec3(0.0f);
    float Yaw = -90.0f; // Degrees, about world +Y: -90 looks along -Z, as Fenrir::Camera's yaw.
    float Pitch = 0.0f; // Degrees above the horizon.
    std::int32_t Active = 0;
};

/**
 * @brief In Update, after the free-fly CameraController and the C# Update systems and before the renderer: copies the
 * active scene's DemoCamera (a scene has at most one) into the camera while its Active is set, so it wins over them.
 * With none active the camera is left to the CameraController.
 */
class DemoCameras
{
  public:
    explicit DemoCameras(Fenrir::Camera& camera) : m_camera(&camera)
    {
    }

    void Update(Fenrir::App& app)
    {
        bool copied = false;
        app.GetActiveScene().GetEntityList().ForEach<DemoCamera>([this, &copied](DemoCamera& wanted) {
            if (copied || wanted.Active == 0)
            {
                return;
            }
            copied = true;
            m_camera->pos = wanted.Position;
            m_camera->yaw = wanted.Yaw;
            m_camera->pitch = wanted.Pitch;
            m_camera->Update();
        });
    }

  private:
    Fenrir::Camera* m_camera;
};
