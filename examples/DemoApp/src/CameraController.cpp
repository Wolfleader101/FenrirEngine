#include "CameraController.hpp"

#include "FenrirApp/App.hpp"
#include "FenrirCamera/Camera.hpp"
#include "Window.hpp"

CameraController::CameraController(Fenrir::Camera& camera, float sensitity, float speed)
    : m_camera(camera), m_sensitive(sensitity), m_speed(speed)
{
}

void CameraController::OnKeyPress(const KeyboardKeyEvent& event)
{
    if (event.state == InputState::Pressed)
    {
        m_pressedKeys.insert(event.key);
    }
    else if (event.state == InputState::Released)
    {
        m_pressedKeys.erase(event.key);
    }
}

void CameraController::OnMouseMove(const MouseMoveEvent& event)
{
    float xpos = static_cast<float>(event.x);
    float ypos = static_cast<float>(event.y);

    m_deltaMousePos.x = xpos - m_lastMousePos.x;
    m_deltaMousePos.y = m_lastMousePos.y - ypos;

    m_lastMousePos.x = xpos;
    m_lastMousePos.y = ypos;

    Fenrir::Math::Vec2 deltaWithSens = m_deltaMousePos * m_sensitive;

    m_camera.yaw += deltaWithSens.x;
    m_camera.pitch += deltaWithSens.y;

    if (m_camera.pitch > 89.0f)
        m_camera.pitch = 89.0f;
    if (m_camera.pitch < -89.0f)
        m_camera.pitch = -89.0f;
    m_camera.Update();
}

void CameraController::OnMouseScroll(const MouseScrollEvent& event)
{
    m_camera.fov -= static_cast<float>(event.yOffset);
    if (m_camera.fov < 1.0f)
        m_camera.fov = 1.0f;
}

void CameraController::Update(Fenrir::App& app)
{
    for (const auto& event : app.ReadEvents<KeyboardKeyEvent>())
    {
        OnKeyPress(event);
    }

    for (const auto& event : app.ReadEvents<MouseMoveEvent>())
    {
        OnMouseMove(event);
    }

    for (const auto& event : app.ReadEvents<MouseScrollEvent>())
    {
        OnMouseScroll(event);
    }

    if (m_pressedKeys.count(GLFW_KEY_LEFT_SHIFT))
    {
        m_isSprinting = true;
    }
    else
    {
        m_isSprinting = false;
    }

    float speed = m_isSprinting ? m_sprintSpeed : m_speed;
    float vel = speed * static_cast<float>(app.GetTime().deltaTime);

    if (m_pressedKeys.count(GLFW_KEY_W))
    {
        m_camera.pos += vel * m_camera.front;
    }

    if (m_pressedKeys.count(GLFW_KEY_S))
    {
        m_camera.pos -= vel * m_camera.front;
    }

    if (m_pressedKeys.count(GLFW_KEY_A))
    {
        m_camera.pos -= Fenrir::Math::Normalized(Fenrir::Math::Cross(m_camera.front, m_camera.up)) * vel;
    }

    if (m_pressedKeys.count(GLFW_KEY_D))
    {
        m_camera.pos += Fenrir::Math::Normalized(Fenrir::Math::Cross(m_camera.front, m_camera.up)) * vel;
    }
}
