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

        // TODO setup event listeners
        // look into EventDistacher<T> and EventListener<T> classes

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

    void PostUpdate(Fenrir::App& app)
    {
        //! TEMP FOR TESTING
        if (glfwGetKey(m_window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
            app.Stop();

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