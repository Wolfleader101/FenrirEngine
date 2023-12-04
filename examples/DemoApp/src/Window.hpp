#pragma once
// TODO dont expose this
// clang-format off
#include <glad/glad.h>
#include <GLFW/glfw3.h>
// clang-format on

#include <string>

namespace Fenrir
{
    class App;
    class Camera;
} // namespace Fenrir

struct FrameBufferResizeEvent
{
    int width, height;
};

struct WindowResizeEvent
{
    int width, height;
};

struct WindowCloseEvent
{
};

struct MouseMoveEvent
{
    double x, y;
};

struct MouseScrollEvent
{
    double xOffset, yOffset;
};

enum class MouseButton
{
    Left = 0,
    Right = 1,
    Middle = 2
};

enum class InputState
{
    Released = 0,
    Pressed = 1,
    Held = 2,
};

struct MouseButtonEvent
{
    MouseButton button;
    InputState state;
    int mods;
};

struct KeyboardKeyEvent
{
    int key;
    int scancode;
    int repeat;
    InputState state;
    int mods;
};

class Window
{
  public:
    Window(std::string title = "FenrirEngine Window", int width = 800, int height = 600);

    ~Window();

    void PreInit(Fenrir::App& app);

    void OnKeyPress(const KeyboardKeyEvent& event);

    void PostUpdate(Fenrir::App& app);

    void Exit(Fenrir::App&);

    int GetWidth() const;

    int GetHeight() const;

  private:
    GLFWwindow* m_window = nullptr;
    Fenrir::App* m_appPtr = nullptr;
    std::string m_title = "";
    int m_width = 0;
    int m_height = 0;
};

#define BIND_WINDOW_SYSTEM_FN(fn, windowInstance) std::bind(&Window::fn, &windowInstance, std::placeholders::_1)