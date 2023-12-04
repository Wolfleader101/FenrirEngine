#include "Window.hpp"

#include "FenrirApp/App.hpp"

// Wrapper function that matches the GLADloadproc signature
static void* Glad_GLFW_GetProcAddr(const char* name)
{
    return reinterpret_cast<void*>(glfwGetProcAddress(name));
}

Window::Window(std::string title, int width, int height) : m_title(std::move(title)), m_width(width), m_height(height)
{
}

Window::~Window()
{
    glfwDestroyWindow(m_window);
}

void Window::PreInit(Fenrir::App& app)
{
    m_appPtr = &app;
    app.Logger()->Info("Window Pre-Init");

    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE,
                   GLFW_OPENGL_CORE_PROFILE); // core profile only, no fixed pipeline functionality
    // glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE); // MacOS

    m_window = glfwCreateWindow(m_width, m_height, m_title.c_str(), nullptr, nullptr);

    if (m_window == nullptr)
    {
        app.Logger()->Fatal("Failed to create GLFW window!");

        glfwTerminate();
        // TODO exit the application?
        return;
    }
    glfwMakeContextCurrent(m_window);

    // Not calling: driver default
    // 0: do not wait for vsync (may be overridden by driver/driver settings)
    // 1: wait for 1st vsync (may be overridden by driver/driver settings)
    // glfwSwapInterval(0);

    // cursor mode
    glfwSetInputMode(m_window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    if (!gladLoadGLLoader(Glad_GLFW_GetProcAddr))
    {
        app.Logger()->Fatal("Failed to initialise GLAD");
        // TODO exit the application?
        return;
    }

    //! Setup Event Listeners

    // set the user pointer to this
    glfwSetWindowUserPointer(m_window, this);
    //? argued with myself whether the userPointer should be the application or the window
    //? decided on the window, which meant i needed to store an App* in the window, however provides more
    //? flexibility, and makes more sense as this is a window class

    // set the callback function for framebuffer resize events
    glfwSetFramebufferSizeCallback(m_window, [](GLFWwindow* window, int width, int height) {
        auto& win = *static_cast<Window*>(glfwGetWindowUserPointer(window));

        // TODO remove this as its render specific
        glViewport(0, 0, width, height);

        FrameBufferResizeEvent event{width, height};
        win.m_appPtr->SendEvent(event);
    });

    // set the callback function for window resize events
    glfwSetWindowSizeCallback(m_window, [](GLFWwindow* window, int width, int height) {
        auto& win = *static_cast<Window*>(glfwGetWindowUserPointer(window));

        // TODO remove this as its render specific
        glViewport(0, 0, width, height);

        WindowResizeEvent event{width, height};
        win.m_appPtr->SendEvent(event);
    });

    // set the callback function for window close events
    glfwSetWindowCloseCallback(m_window, [](GLFWwindow* window) {
        auto& win = *static_cast<Window*>(glfwGetWindowUserPointer(window));

        WindowCloseEvent event{};
        win.m_appPtr->SendEvent(event);
    });

    glfwSetKeyCallback(m_window, [](GLFWwindow* window, int key, int scancode, int action, int mods) {
        auto& win = *static_cast<Window*>(glfwGetWindowUserPointer(window));

        InputState state = (action == GLFW_PRESS)     ? InputState::Pressed
                           : (action == GLFW_RELEASE) ? InputState::Released
                           : (action == GLFW_REPEAT)  ? InputState::Held
                                                      : InputState::Released;

        KeyboardKeyEvent event{key, scancode, action, state, mods};
        win.m_appPtr->SendEvent(event);
    });

    glfwSetMouseButtonCallback(m_window, [](GLFWwindow* window, int button, int action, int mods) {
        auto& win = *static_cast<Window*>(glfwGetWindowUserPointer(window));

        InputState state = (action == GLFW_PRESS) ? InputState::Pressed
                           : (action == GLFW_RELEASE)
                               ? InputState::Released
                               : InputState::Released; // GLFW doesnt have GLFW_REPEAT for mouse buttons

        MouseButton btn = (button == GLFW_MOUSE_BUTTON_LEFT)     ? MouseButton::Left
                          : (button == GLFW_MOUSE_BUTTON_RIGHT)  ? MouseButton::Right
                          : (button == GLFW_MOUSE_BUTTON_MIDDLE) ? MouseButton::Middle
                                                                 : MouseButton::Left;

        MouseButtonEvent event{btn, state, mods};
        win.m_appPtr->SendEvent(event);
    });

    glfwSetScrollCallback(m_window, [](GLFWwindow* window, double xoffset, double yoffset) {
        auto& win = *static_cast<Window*>(glfwGetWindowUserPointer(window));

        MouseScrollEvent event{xoffset, yoffset};
        win.m_appPtr->SendEvent(event);
    });

    glfwSetCursorPosCallback(m_window, [](GLFWwindow* window, double xpos, double ypos) {
        auto& win = *static_cast<Window*>(glfwGetWindowUserPointer(window));

        MouseMoveEvent event{xpos, ypos};
        win.m_appPtr->SendEvent(event);
    });
}

void Window::OnKeyPress(const KeyboardKeyEvent& event)
{
    if (event.key == GLFW_KEY_ESCAPE && event.state == InputState::Pressed)
    {
        m_appPtr->Stop();
    }
}

void Window::PostUpdate(Fenrir::App& app)
{
    if (app.ReadEvents<WindowCloseEvent>().size() > 0)
    {
        app.Stop();
    }

    for (const auto& event : app.ReadEvents<KeyboardKeyEvent>())
    {
        OnKeyPress(event);
    }

    // swap the buffers and then process events
    glfwSwapBuffers(m_window);

    glfwPollEvents();
}

void Window::Exit(Fenrir::App&)
{
    glfwTerminate();
}

int Window::GetWidth() const
{
    return m_width;
}

int Window::GetHeight() const
{
    return m_height;
}
