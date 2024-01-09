#pragma once
// TODO dont expose this
// clang-format off
#include <glad/glad.h>
#include <GLFW/glfw3.h>
// clang-format on

#include <string>

#include "Events.hpp"

namespace Fenrir
{
    class App;
    class Camera;
} // namespace Fenrir

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

    GLFWwindow* GetGlWindow() const;

  private:
    GLFWwindow* m_window = nullptr;
    Fenrir::App* m_appPtr = nullptr;
    std::string m_title = "";
    int m_width = 0;
    int m_height = 0;
};

#define BIND_WINDOW_SYSTEM_FN(fn, windowInstance) std::bind(&Window::fn, &windowInstance, std::placeholders::_1)