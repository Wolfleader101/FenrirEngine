// clang-format off
#include <glad/glad.h>
#include <GLFW/glfw3.h>
// clang-format on

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image/stb_image.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "FenrirApp/App.hpp"
#include "FenrirLogger/ConsoleLogger.hpp"

static void systemA(Fenrir::App&)
{
}

static void systemB(Fenrir::App&)
{
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
    int mods;
};

class Shader
{
  public:
    unsigned int Id;

    /**
     * @brief Construct a new Shader object
     *
     * @param logger the logger to use
     * @param vertexPath the path to the vertex shader
     * @param fragmentPath the path to the fragment shader
     */
    Shader(Fenrir::ILogger* logger, std::string vertexPath, std::string fragmentPath)
    {
        // TODO eventually move this code to shader library/Asset manager to load shaders from files

        std::string vertexCode;
        std::string fragmentCode;

        std::ifstream vShaderFile;
        std::ifstream fShaderFile;

        vShaderFile.exceptions(std::ifstream::failbit | std::ifstream::badbit);
        fShaderFile.exceptions(std::ifstream::failbit | std::ifstream::badbit);

        try
        {
            vShaderFile.open(vertexPath);
            fShaderFile.open(fragmentPath);

            std::stringstream vShaderStream, fShaderStream;

            vShaderStream << vShaderFile.rdbuf();
            fShaderStream << fShaderFile.rdbuf();

            vShaderFile.close();
            fShaderFile.close();

            vertexCode = vShaderStream.str();
            fragmentCode = fShaderStream.str();
        }
        catch (std::ifstream::failure& e)
        {
            logger->Fatal("COULD NOT READ SHADERS {0} {1}, message: {2}", vertexPath, fragmentPath, e.what());
        }

        const char* vShaderCode = vertexCode.c_str();
        const char* fShaderCode = fragmentCode.c_str();

        unsigned int vertex, fragment;
        int success = -1;
        char infoLog[512] = {0};

        // create the vertex shader
        vertex = glCreateShader(GL_VERTEX_SHADER);

        // attach the shader source code to the shader object and compile the shader
        glShaderSource(vertex, 1, &vShaderCode, nullptr);

        // compile the shader
        glCompileShader(vertex);

        // check for compilation errors
        glGetShaderiv(vertex, GL_COMPILE_STATUS, &success);
        if (!success)
        {
            glGetShaderInfoLog(vertex, 512, nullptr, infoLog);
            logger->Fatal("VERTEX SHADER COMPILATION FAILED\n{0}", infoLog);
        }

        // create the fragment shader
        fragment = glCreateShader(GL_FRAGMENT_SHADER);

        // attach the shader source code to the shader object and compile the shader
        glShaderSource(fragment, 1, &fShaderCode, nullptr);

        // compile the shader
        glCompileShader(fragment);

        // check for compilation errors
        glGetShaderiv(fragment, GL_COMPILE_STATUS, &success);
        if (!success)
        {
            glGetShaderInfoLog(fragment, 512, nullptr, infoLog);
            logger->Fatal("FRAGMENT SHADER COMPILATION FAILED\n{0}", infoLog);
        }

        // create a shader program object, store its ID in Id
        Id = glCreateProgram();

        // attach the vertex shader and fragment shader to the shader program
        glAttachShader(Id, vertex);
        glAttachShader(Id, fragment);

        // link the shader program
        glLinkProgram(Id);

        // check for linking errors
        glGetProgramiv(Id, GL_LINK_STATUS, &success);
        if (!success)
        {
            glGetProgramInfoLog(Id, 512, nullptr, infoLog);
            logger->Fatal("SHADER PROGRAM LINKING FAILED\n{0}", infoLog);
        }

        // delete the shaders as they have been linked and not needed
        glDeleteShader(vertex);
        glDeleteShader(fragment);
    }

    ~Shader()
    {
        glDeleteProgram(Id);
    }

    /**
     * @brief use the shader program
     *
     */
    void Use()
    {
        glUseProgram(Id);
    }

    void SetBool(const std::string& name, bool value) const
    {
        glUniform1i(glGetUniformLocation(Id, name.c_str()), static_cast<int>(value));
    }

    void SetInt(const std::string& name, int value) const
    {
        glUniform1i(glGetUniformLocation(Id, name.c_str()), value);
    }

    void SetFloat(const std::string& name, float value) const
    {
        glUniform1f(glGetUniformLocation(Id, name.c_str()), value);
    }
};

static void LoadImage(Fenrir::ILogger* logger, const char* path, unsigned int& textureId)
{
    glGenTextures(1, &textureId);
    glBindTexture(GL_TEXTURE_2D, textureId);

    // set texture filter options on current texture object
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    int width, height, nrChannels;
    stbi_set_flip_vertically_on_load(true);
    unsigned char* data = stbi_load(path, &width, &height, &nrChannels, 0);

    if (data)
    {
        // generate the texture
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, data);
        glGenerateMipmap(GL_TEXTURE_2D);
    }
    else
    {
        logger->Fatal("Failed to load texture");
    }

    stbi_image_free(data);
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

        //? GL SPECIFIC CODE FOR TESTING
        glViewport(0, 0, m_width, m_height);

        //! SHADERS
        Fenrir::ILogger* logger = app.Logger().get();
        m_shader = std::make_unique<Shader>(logger, std::string("assets/shaders/vertex.glsl"),
                                            std::string("assets/shaders/fragment.glsl"));

        //! TEXTURES
        glActiveTexture(GL_TEXTURE0);
        LoadImage(logger, "assets/textures/mortar-bricks/mortar-bricks_albedo.png", textureId1);

        glActiveTexture(GL_TEXTURE1);
        LoadImage(logger, "assets/textures/art-deco-scales/art-deco-scales_albedo.png", textureId2);

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

        // position attribute
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), nullptr);
        glEnableVertexAttribArray(0);

        // color attribute
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
        glEnableVertexAttribArray(1);

        // texture coord attribute
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));
        glEnableVertexAttribArray(2);

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

        // glPolygonMode(GL_FRONT_AND_BACK, GL_LINE); // wireframe mode
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL); // fill mode

        m_shader->Use();
        m_shader->SetInt("texture1", 0);
        m_shader->SetInt("texture2", 1);

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

        glfwTerminate();
    }

  private:
    GLFWwindow* m_window = nullptr;
    Fenrir::App* m_appPtr = nullptr;
    std::unique_ptr<Shader> m_shader = nullptr;

    std::string m_title = "";
    int m_width = 0;
    int m_height = 0;

    float vertices[32] = {
        // positions          // colors           // texture coords
        0.5f,  0.5f,  0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, // top right
        0.5f,  -0.5f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, // bottom right
        -0.5f, -0.5f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, // bottom left
        -0.5f, 0.5f,  0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f  // top left
    };

    unsigned int indices[6] = {
        0, 1, 3, // first triangle
        1, 2, 3  // second triangle
    };

    unsigned int VBO;

    unsigned int VAO;

    unsigned int EBO;

    unsigned int textureId1;
    unsigned int textureId2;
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