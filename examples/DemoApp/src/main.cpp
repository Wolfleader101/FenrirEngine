// clang-format off
#include <glad/glad.h>
#include <GLFW/glfw3.h>
// clang-format on

#include <iostream>

#include "FenrirApp/App.hpp"
#include "FenrirLogger/ConsoleLogger.hpp"
// struct MouseMoveEvent
// {
//     float x, y;
//     float deltaX, deltaY;
// };

static void systemA(Fenrir::App&)
{
    // app.Logger()->Warn("System A {0}", 1);
    //     float last_x = rand() % 50, last_y = rand() % 50;
    //     float new_x = rand() % 100, new_y = rand() % 100;

    //     // send mock event
    //     MouseMoveEvent event{new_x, new_y, new_x - last_x, new_y - last_y};
    //     app.SendEvent(event);
}

static void systemB(Fenrir::App&)
{
    // mock handle the event
    // for (const auto& event : app.ReadEvents<MouseMoveEvent>())
    // {
    //     std::cout << "Mouse moved to (" << event.x << ", " << event.y << ")" << std::endl;
    // }
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
};

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
        // glfwSetInputMode(m_window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

        if (!gladLoadGLLoader(Glad_GLFW_GetProcAddr))
        {
            app.Logger()->Fatal("Failed to initialise GLAD");
            // TODO exit the application?
            return;
        }

        // Setup Event Listeners

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

            KeyboardKeyEvent event{key, scancode, action, state};
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

        //? GL SPECIFIC CODE FOR TESTING
        glViewport(0, 0, m_width, m_height);

        //! SHADERS
        // create a vertex shader object, store its ID in vertexShaderId
        vertexShaderId = glCreateShader(GL_VERTEX_SHADER);

        // attach the shader source code to the shader object and compile the shader
        glShaderSource(vertexShaderId, 1, &vertex_shader_code, nullptr);

        // compile the shader
        glCompileShader(vertexShaderId);

        // check for compilation errors
        int success = 0;
        char infoLog[512] = {0};

        // get the compilation status
        glGetShaderiv(vertexShaderId, GL_COMPILE_STATUS, &success);

        if (!success)
        {
            // get the error message
            glGetShaderInfoLog(vertexShaderId, 512, nullptr, infoLog);
            app.Logger()->Fatal("ERROR::SHADER::VERTEX::COMPILATION_FAILED\n{0}", infoLog);
            return;
        }

        // create a fragment shader object, store its ID in fragmentShaderId
        fragmentShaderId = glCreateShader(GL_FRAGMENT_SHADER);

        // attach the shader source code to the shader object and compile the shader
        glShaderSource(fragmentShaderId, 1, &fragment_shader_code, nullptr);

        // compile the shader
        glCompileShader(fragmentShaderId);

        // check for compilation errors
        glGetShaderiv(fragmentShaderId, GL_COMPILE_STATUS, &success);

        if (!success)
        {
            // get the error message
            glGetShaderInfoLog(fragmentShaderId, 512, nullptr, infoLog);
            app.Logger()->Fatal("ERROR::SHADER::FRAGMENT::COMPILATION_FAILED\n{0}", infoLog);
            return;
        }

        // create a shader program object, store its ID in shaderProgramId
        shaderProgramId = glCreateProgram();

        // attach the vertex shader and fragment shader to the shader program
        glAttachShader(shaderProgramId, vertexShaderId);
        glAttachShader(shaderProgramId, fragmentShaderId);

        // link the shader program
        glLinkProgram(shaderProgramId);

        // check for linking errors
        glGetProgramiv(shaderProgramId, GL_LINK_STATUS, &success);

        if (!success)
        {
            // get the error message
            glGetProgramInfoLog(shaderProgramId, 512, nullptr, infoLog);
            app.Logger()->Fatal("ERROR::SHADER::PROGRAM::LINKING_FAILED\n{0}", infoLog);
            return;
        }

        // delete the shader objects once they've been linked into the program object
        glDeleteShader(vertexShaderId);
        glDeleteShader(fragmentShaderId);

        //! VERTEX DATA AND BUFFERS

        // create a vertex buffer object, store its ID in VBO
        glGenBuffers(1, &VBO);

        // bind the buffer to the GL_ARRAY_BUFFER target (any calls made to GL_ARRAY_BUFFER will affect VBO)
        glBindBuffer(GL_ARRAY_BUFFER, VBO);

        // copy the vertex data into the buffer's memory
        glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

        // create an element buffer object, store its ID in EBO
        glGenBuffers(1, &EBO);

        // bind the buffer to the GL_ELEMENT_ARRAY_BUFFER target
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);

        // copy the vertex data into the buffer's memory
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

        // create a vertex array object, store its ID in VAO
        glGenVertexArrays(1, &VAO);

        // initialise VAO only once
        glBindVertexArray(VAO);

        // bind the vertex buffer object
        glBindBuffer(GL_ARRAY_BUFFER, VBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

        // bind the element buffer object
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

        // tell OpenGL how to interpret the vertex data (per vertex attribute)
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
        glEnableVertexAttribArray(0);

        // unbind the VBO and VAO
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindVertexArray(0);
        // unbinding is not always needed as a VAO is is created and bound before other objects are bound to it
    }

    void OnKeyPress(const KeyboardKeyEvent& event)
    {
        if (event.key == GLFW_KEY_ESCAPE && event.state == InputState::Pressed)
        {
            m_appPtr->Stop();
        }
    }

    void PostUpdate(Fenrir::App& app)
    {
        for (const auto& event : app.ReadEvents<KeyboardKeyEvent>())
        {
            OnKeyPress(event);
        }

        glClearColor(0.45f, 0.6f, 0.75f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        // draw the triangle
        // glPolygonMode(GL_FRONT_AND_BACK, GL_LINE); // wireframe mode
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL); // fill mode
        glUseProgram(shaderProgramId);
        glBindVertexArray(VAO);
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr);
        // glBindVertexArray(0); // dont need to unbind every time

        // swap the buffers and then process events
        glfwSwapBuffers(m_window);

        glfwPollEvents();
    }

    void Exit(Fenrir::App&)
    {
        // cleanup of resources
        glDeleteVertexArrays(1, &VAO);
        glDeleteBuffers(1, &VBO);
        glDeleteBuffers(1, &EBO);
        glDeleteProgram(shaderProgramId);

        glfwTerminate();
    }

  private:
    GLFWwindow* m_window = nullptr;
    Fenrir::App* m_appPtr = nullptr;

    std::string m_title = "";
    int m_width = 0;
    int m_height = 0;

    float vertices[12] = {
        0.5f,  0.5f,  0.0f, // top right
        0.5f,  -0.5f, 0.0f, // bottom right
        -0.5f, -0.5f, 0.0f, // bottom left
        -0.5f, 0.5f,  0.0f  // top left
    };

    unsigned int indices[6] = {
        0, 1, 3, // first triangle
        1, 2, 3  // second triangle
    };

    unsigned int VBO;

    unsigned int vertexShaderId;
    const char* vertex_shader_code = "#version 460 core\n"
                                     "layout (location = 0) in vec3 aPos;\n"
                                     "void main()\n"
                                     "{\n"
                                     "   gl_Position = vec4(aPos.x, aPos.y, aPos.z, 1.0);\n"
                                     "}\0";

    unsigned int fragmentShaderId;
    const char* fragment_shader_code = "#version 460 core\n"
                                       "out vec4 FragColor;\n"
                                       "void main()\n"
                                       "{\n"
                                       "   FragColor = vec4(0.8f, 0.6f, 0.15f, 1.0f);\n"
                                       "}\0";

    unsigned int shaderProgramId;

    unsigned int VAO;

    unsigned int EBO;
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