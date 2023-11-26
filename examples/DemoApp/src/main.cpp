// clang-format off
#include <glad/glad.h>
#include <GLFW/glfw3.h>
// clang-format on

#include <iostream>

#include "FenrirApp/App.hpp"
#include "FenrirLogger/ConsoleLogger.hpp"

static void systemA(Fenrir::App&)
{
    // app.Logger()->Warn("System A {0}", 1);
}

static void systemB(Fenrir::App&)
{
    // app.Logger()->Error("System B");
}

static void Tick(Fenrir::App&)
{
    // app.Logger()->Log("Tick");
}

static void PreInit(Fenrir::App& app)
{
    app.Logger()->Info("PreInit");
}

static void Init(Fenrir::App& app)
{
    app.Logger()->Info("Init");
}

static void PostInit(Fenrir::App& app)
{
    app.Logger()->Fatal("PostInit");
}

// Wrapper function that matches the GLADloadproc signature
static void* Glad_GLFW_GetProcAddr(const char* name)
{
    return reinterpret_cast<void*>(glfwGetProcAddress(name));
}

class Window
{
  public:
    Window(std::string title = "FenrirEngine Window", int width = 800, int height = 600)
        : m_title(std::move(title)), m_width(width), m_height(height)
    {
    }

    ~Window()
    {
        glfwDestroyWindow(m_window);
    }

    void PreInit(Fenrir::App& app)
    {
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
        // glfwSetInputMode(m_window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

        if (!gladLoadGLLoader(Glad_GLFW_GetProcAddr))
        {
            app.Logger()->Fatal("Failed to initialise GLAD");
            // TODO exit the application?
            return;
        }

        //! GL SPECIFIC CODE
        glViewport(0, 0, m_width, m_height);

        // TODO setup event listeners
        // look into EventDistacher<T> and EventListener<T> classes
    }

    void PostUpdate(Fenrir::App& app)
    {
        //! TEMP FOR TESTING
        if (glfwGetKey(m_window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
            app.Stop();

        glClearColor(0.45f, 0.6f, 0.75f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        // swap the buffers and then process events
        glfwSwapBuffers(m_window);

        glfwPollEvents();
    }

    void Exit(Fenrir::App&)
    {
        glfwTerminate();
    }

  private:
    GLFWwindow* m_window = nullptr;

    std::string m_title = "";
    int m_width = 0;
    int m_height = 0;
};
#define BIND_WINDOW_SYSTEM_FN(fn, windowInstance) std::bind(&Window::fn, &windowInstance, std::placeholders::_1)

int main()
{
    auto logger = std::make_unique<Fenrir::ConsoleLogger>();
    Fenrir::App app(std::move(logger));

    Window window("Demo App");

    app.AddSystems(Fenrir::SchedulePriority::PreInit, {PreInit, BIND_WINDOW_SYSTEM_FN(Window::PreInit, window)})
        .AddSystems(Fenrir::SchedulePriority::Init, {Init})
        .AddSystems(Fenrir::SchedulePriority::PostInit, {PostInit})
        .AddSystems(Fenrir::SchedulePriority::Update, {systemB})
        .AddSystems(Fenrir::SchedulePriority::PreUpdate, {systemA})
        .AddSystems(Fenrir::SchedulePriority::Tick, {Tick})
        .AddSystems(Fenrir::SchedulePriority::PostUpdate, {BIND_WINDOW_SYSTEM_FN(Window::PostUpdate, window)})
        .AddSystems(Fenrir::SchedulePriority::Exit, {BIND_WINDOW_SYSTEM_FN(Window::Exit, window)})
        .Run();
}