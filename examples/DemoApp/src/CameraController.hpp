#pragma once

#include <unordered_set>

#include "FenrirMath/Math.hpp"

namespace Fenrir
{
    class App;
    class Camera;
} // namespace Fenrir

#include "Window.hpp"

class CameraController
{
  public:
    CameraController(Fenrir::Camera& camera, float sensitity, float speed);

    void OnKeyPress(const KeyboardKeyEvent& event);

    void OnMouseMove(const MouseMoveEvent& event);

    void OnMouseScroll(const MouseScrollEvent& event);

    void Update(Fenrir::App& app);

  private:
    Fenrir::Camera& m_camera;
    float m_sensitive = 0.1f;
    float m_speed = 3.0f;
    float m_sprintSpeed = 6.0f;
    bool m_isSprinting = false;
    Fenrir::Math::Vec2 m_lastMousePos = Fenrir::Math::Vec2(400.0f, 300.0f);

    // TODO move to a custom input class
    std::unordered_set<int> m_pressedKeys;

    Fenrir::Math::Vec2 m_deltaMousePos = Fenrir::Math::Vec2(0.0f, 0.0f);
};

#define BIND_CAMERA_CONTROLLER_FN(fn, controllerInstance) \
    std::bind(&CameraController::fn, &controllerInstance, std::placeholders::_1)
