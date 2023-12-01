// clang-format off
#include <glad/glad.h>
#include <GLFW/glfw3.h>
// clang-format on

#include <string>
#include <unordered_set>

#include "ShaderLibrary.hpp"
#include "TextureLibrary.hpp"

#include "FenrirApp/App.hpp"
#include "FenrirCamera/Camera.hpp"
#include "FenrirLogger/ConsoleLogger.hpp"
#include "FenrirMath/Math.hpp"

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

struct Transform
{
    Fenrir::Math::Vec3 pos;
    Fenrir::Math::Vec3 rot; // TODO convert to quat
    Fenrir::Math::Vec3 scale;
};

class CameraController
{
  public:
    CameraController(Fenrir::Camera& camera, float sensitity, float speed)
        : m_camera(camera), m_sensitive(sensitity), m_speed(speed)
    {
    }

    void OnKeyPress(const KeyboardKeyEvent& event)
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

    void OnMouseMove(const MouseMoveEvent& event)
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

    void OnMouseScroll(const MouseScrollEvent& event)
    {
        m_camera.fov -= static_cast<float>(event.yOffset);
        if (m_camera.fov < 1.0f)
            m_camera.fov = 1.0f;
    }

    void Update(Fenrir::App& app)
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

class Window
{
  public:
    Window(Fenrir::Camera& camera, std::string title = "FenrirEngine Window", int width = 800, int height = 600)
        : m_title(std::move(title)), m_width(width), m_height(height), m_camera(camera)
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

        //? GL SPECIFIC CODE FOR TESTING
        glViewport(0, 0, m_width, m_height);

        glEnable(GL_DEPTH_TEST);

        //! SHADERS
        Fenrir::ILogger* logger = app.Logger().get();
        // m_shader = std::make_unique<Shader>(logger, std::string("assets/shaders/vertex.glsl"),
        //                                     std::string("assets/shaders/fragment.glsl"));
        m_shader = std::make_unique<Shader>(logger, std::string("assets/shaders/lighted_vertex.glsl"),
                                            std::string("assets/shaders/lighted_fragment.glsl"));

        m_lightShader = std::make_unique<Shader>(logger, std::string("assets/shaders/light_vertex.glsl"),
                                                 std::string("assets/shaders/light_fragment.glsl"));
        //! TEXTURES
        glActiveTexture(GL_TEXTURE0);
        diffuseId = LoadTexture(logger, "assets/textures/art-deco-scales/art-deco-scales_albedo.png");

        glActiveTexture(GL_TEXTURE1);
        specularId = LoadTexture(logger, "assets/textures/art-deco-scales/art-deco-scales_metallic.png");

        // diffuseId = LoadTexture(logger, "assets/textures/mortar-bricks/mortar-bricks_albedo.png");
        // specularId = LoadTexture(logger, "assets/textures/mortar-bricks/mortar-bricks_metallic.png");
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

        // normal attribute
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), reinterpret_cast<void*>(3 * sizeof(float)));
        glEnableVertexAttribArray(1);

        // texture coord attribute
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), reinterpret_cast<void*>(6 * sizeof(float)));
        glEnableVertexAttribArray(2);

        // unbind the VBO and VAO
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindVertexArray(0);
        // unbinding is not always needed as a VAO is is created and bound before other objects are bound to it

        glGenVertexArrays(1, &lightVAO);
        glBindVertexArray(lightVAO);

        // use pre exising VBO
        glBindBuffer(GL_ARRAY_BUFFER, VBO);

        // position attribute
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), nullptr);
        glEnableVertexAttribArray(0);

        m_shader->Use();

        // setting material diffuse can be set once
        m_shader->SetInt("material.diffuse", 0);
        // setting material specular can be set once
        m_shader->SetInt("material.specular", 1);
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
        if (app.ReadEvents<WindowCloseEvent>().size() > 0)
        {
            app.Stop();
        }

        for (const auto& event : app.ReadEvents<KeyboardKeyEvent>())
        {
            OnKeyPress(event);
        }

        // glClearColor(0.45f, 0.6f, 0.75f, 1.0f); // vakol blue
        glClearColor(0.0941176f, 0.0941176f, 0.0941176f, 1.0f); // dark grey
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // glPolygonMode(GL_FRONT_AND_BACK, GL_LINE); // wireframe mode
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL); // fill mode

        Fenrir::Math::Mat4 view = m_camera.GetViewMatrix();

        Fenrir::Math::Mat4 projection = Fenrir::Math::Perspective(Fenrir::Math::DegToRad(m_camera.fov),
                                                                  static_cast<float>(m_width / m_height), 0.1f, 100.0f);

        //! DRAWING LIGHTS
        m_lightShader->Use();
        m_lightShader->SetMat4("view", view);
        m_lightShader->SetMat4("projection", projection);

        glBindVertexArray(lightVAO);
        for (unsigned int i = 0; i < 4; i++)
        {
            // calculate the model matrix for each object and pass it to shader before drawing
            Fenrir::Math::Mat4 model = Fenrir::Math::Mat4(1.0f);
            model = Fenrir::Math::Translate(model, pointLightPositions[i]);
            model = glm::scale(model, glm::vec3(0.2f));
            m_lightShader->SetMat4("model", model);

            glDrawArrays(GL_TRIANGLES, 0, 36);
        }

        //! DRAWING CUBES
        m_shader->Use();

        m_shader->SetMat4("view", view);
        m_shader->SetMat4("projection", projection);

        // directional light
        m_shader->SetVec3("dirLight.direction", -0.2f, -1.0f, -0.3f);
        m_shader->SetVec3("dirLight.ambient", 0.05f, 0.05f, 0.05f);
        m_shader->SetVec3("dirLight.diffuse", 0.4f, 0.4f, 0.4f);
        m_shader->SetVec3("dirLight.specular", 0.5f, 0.5f, 0.5f);
        // point light 1
        m_shader->SetVec3("pointLights[0].pos", pointLightPositions[0]);
        m_shader->SetVec3("pointLights[0].ambient", 0.05f, 0.05f, 0.05f);
        m_shader->SetVec3("pointLights[0].diffuse", 0.8f, 0.8f, 0.8f);
        m_shader->SetVec3("pointLights[0].specular", 1.0f, 1.0f, 1.0f);
        m_shader->SetFloat("pointLights[0].constant", 1.0f);
        m_shader->SetFloat("pointLights[0].linear", 0.09f);
        m_shader->SetFloat("pointLights[0].quadratic", 0.032f);
        // point light 2
        m_shader->SetVec3("pointLights[1].pos", pointLightPositions[1]);
        m_shader->SetVec3("pointLights[1].ambient", 0.05f, 0.05f, 0.05f);
        m_shader->SetVec3("pointLights[1].diffuse", 0.8f, 0.8f, 0.8f);
        m_shader->SetVec3("pointLights[1].specular", 1.0f, 1.0f, 1.0f);
        m_shader->SetFloat("pointLights[1].constant", 1.0f);
        m_shader->SetFloat("pointLights[1].linear", 0.09f);
        m_shader->SetFloat("pointLights[1].quadratic", 0.032f);
        // point light 3
        m_shader->SetVec3("pointLights[2].pos", pointLightPositions[2]);
        m_shader->SetVec3("pointLights[2].ambient", 0.05f, 0.05f, 0.05f);
        m_shader->SetVec3("pointLights[2].diffuse", 0.8f, 0.8f, 0.8f);
        m_shader->SetVec3("pointLights[2].specular", 1.0f, 1.0f, 1.0f);
        m_shader->SetFloat("pointLights[2].constant", 1.0f);
        m_shader->SetFloat("pointLights[2].linear", 0.09f);
        m_shader->SetFloat("pointLights[2].quadratic", 0.032f);
        // point light 4
        m_shader->SetVec3("pointLights[3].pos", pointLightPositions[3]);
        m_shader->SetVec3("pointLights[3].ambient", 0.05f, 0.05f, 0.05f);
        m_shader->SetVec3("pointLights[3].diffuse", 0.8f, 0.8f, 0.8f);
        m_shader->SetVec3("pointLights[3].specular", 1.0f, 1.0f, 1.0f);
        m_shader->SetFloat("pointLights[3].constant", 1.0f);
        m_shader->SetFloat("pointLights[3].linear", 0.09f);
        m_shader->SetFloat("pointLights[3].quadratic", 0.032f);
        // spotLight
        m_shader->SetVec3("spotLight.pos", m_camera.pos);
        m_shader->SetVec3("spotLight.direction", m_camera.front);
        m_shader->SetVec3("spotLight.ambient", 0.0f, 0.0f, 0.0f);
        m_shader->SetVec3("spotLight.diffuse", 1.0f, 1.0f, 1.0f);
        m_shader->SetVec3("spotLight.specular", 1.0f, 1.0f, 1.0f);
        m_shader->SetFloat("spotLight.constant", 1.0f);
        m_shader->SetFloat("spotLight.linear", 0.09f);
        m_shader->SetFloat("spotLight.quadratic", 0.032f);
        m_shader->SetFloat("spotLight.cutOff", glm::cos(glm::radians(12.5f)));
        m_shader->SetFloat("spotLight.outerCutOff", glm::cos(glm::radians(15.0f)));

        // set material in shader (diffuse and specular is set as texture once above)
        m_shader->SetFloat("material.shininess", 32.0f); // bind diffuse map

        // setting material diffuse can be set once
        // m_shader->SetInt("material.diffuse", 0);
        // glActiveTexture(GL_TEXTURE0);
        // glBindTexture(GL_TEXTURE_2D, diffuseId);

        // // setting material specular can be set once
        // m_shader->SetInt("material.specular", 1);
        // glActiveTexture(GL_TEXTURE1);
        // glBindTexture(GL_TEXTURE_2D, specularId);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, diffuseId);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, specularId);

        glBindVertexArray(VAO);

        for (unsigned int i = 0; i < 10; i++)
        {
            glm::mat4 model = glm::mat4(1.0f);
            model = glm::translate(model, cubePositions[i]);
            float angle = 20.0f * i;
            model = glm::rotate(model, Fenrir::Math::DegToRad(angle), glm::vec3(1.0f, 0.3f, 0.5f));
            m_shader->SetMat4("model", model);

            glDrawArrays(GL_TRIANGLES, 0, 36);
        }
        // glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr);

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
    std::string m_title = "";
    int m_width = 0;
    int m_height = 0;

    // GL SPECIFIC

    float vertices[288] = {
        // positions          // normals           // texture coords
        -0.5f, -0.5f, -0.5f, 0.0f,  0.0f,  -1.0f, 0.0f, 0.0f, 0.5f,  -0.5f, -0.5f, 0.0f,  0.0f,  -1.0f, 1.0f, 0.0f,
        0.5f,  0.5f,  -0.5f, 0.0f,  0.0f,  -1.0f, 1.0f, 1.0f, 0.5f,  0.5f,  -0.5f, 0.0f,  0.0f,  -1.0f, 1.0f, 1.0f,
        -0.5f, 0.5f,  -0.5f, 0.0f,  0.0f,  -1.0f, 0.0f, 1.0f, -0.5f, -0.5f, -0.5f, 0.0f,  0.0f,  -1.0f, 0.0f, 0.0f,

        -0.5f, -0.5f, 0.5f,  0.0f,  0.0f,  1.0f,  0.0f, 0.0f, 0.5f,  -0.5f, 0.5f,  0.0f,  0.0f,  1.0f,  1.0f, 0.0f,
        0.5f,  0.5f,  0.5f,  0.0f,  0.0f,  1.0f,  1.0f, 1.0f, 0.5f,  0.5f,  0.5f,  0.0f,  0.0f,  1.0f,  1.0f, 1.0f,
        -0.5f, 0.5f,  0.5f,  0.0f,  0.0f,  1.0f,  0.0f, 1.0f, -0.5f, -0.5f, 0.5f,  0.0f,  0.0f,  1.0f,  0.0f, 0.0f,

        -0.5f, 0.5f,  0.5f,  -1.0f, 0.0f,  0.0f,  1.0f, 0.0f, -0.5f, 0.5f,  -0.5f, -1.0f, 0.0f,  0.0f,  1.0f, 1.0f,
        -0.5f, -0.5f, -0.5f, -1.0f, 0.0f,  0.0f,  0.0f, 1.0f, -0.5f, -0.5f, -0.5f, -1.0f, 0.0f,  0.0f,  0.0f, 1.0f,
        -0.5f, -0.5f, 0.5f,  -1.0f, 0.0f,  0.0f,  0.0f, 0.0f, -0.5f, 0.5f,  0.5f,  -1.0f, 0.0f,  0.0f,  1.0f, 0.0f,

        0.5f,  0.5f,  0.5f,  1.0f,  0.0f,  0.0f,  1.0f, 0.0f, 0.5f,  0.5f,  -0.5f, 1.0f,  0.0f,  0.0f,  1.0f, 1.0f,
        0.5f,  -0.5f, -0.5f, 1.0f,  0.0f,  0.0f,  0.0f, 1.0f, 0.5f,  -0.5f, -0.5f, 1.0f,  0.0f,  0.0f,  0.0f, 1.0f,
        0.5f,  -0.5f, 0.5f,  1.0f,  0.0f,  0.0f,  0.0f, 0.0f, 0.5f,  0.5f,  0.5f,  1.0f,  0.0f,  0.0f,  1.0f, 0.0f,

        -0.5f, -0.5f, -0.5f, 0.0f,  -1.0f, 0.0f,  0.0f, 1.0f, 0.5f,  -0.5f, -0.5f, 0.0f,  -1.0f, 0.0f,  1.0f, 1.0f,
        0.5f,  -0.5f, 0.5f,  0.0f,  -1.0f, 0.0f,  1.0f, 0.0f, 0.5f,  -0.5f, 0.5f,  0.0f,  -1.0f, 0.0f,  1.0f, 0.0f,
        -0.5f, -0.5f, 0.5f,  0.0f,  -1.0f, 0.0f,  0.0f, 0.0f, -0.5f, -0.5f, -0.5f, 0.0f,  -1.0f, 0.0f,  0.0f, 1.0f,

        -0.5f, 0.5f,  -0.5f, 0.0f,  1.0f,  0.0f,  0.0f, 1.0f, 0.5f,  0.5f,  -0.5f, 0.0f,  1.0f,  0.0f,  1.0f, 1.0f,
        0.5f,  0.5f,  0.5f,  0.0f,  1.0f,  0.0f,  1.0f, 0.0f, 0.5f,  0.5f,  0.5f,  0.0f,  1.0f,  0.0f,  1.0f, 0.0f,
        -0.5f, 0.5f,  0.5f,  0.0f,  1.0f,  0.0f,  0.0f, 0.0f, -0.5f, 0.5f,  -0.5f, 0.0f,  1.0f,  0.0f,  0.0f, 1.0f};

    const glm::vec3 pointLightPositions[4] = {glm::vec3(0.7f, 0.2f, 2.0f), glm::vec3(2.3f, -3.3f, -4.0f),
                                              glm::vec3(-4.0f, 2.0f, -12.0f), glm::vec3(0.0f, 0.0f, -3.0f)};

    const glm::vec3 cubePositions[10] = {glm::vec3(0.0f, 0.0f, 0.0f),    glm::vec3(2.0f, 5.0f, -15.0f),
                                         glm::vec3(-1.5f, -2.2f, -2.5f), glm::vec3(-3.8f, -2.0f, -12.3f),
                                         glm::vec3(2.4f, -0.4f, -3.5f),  glm::vec3(-1.7f, 3.0f, -7.5f),
                                         glm::vec3(1.3f, -2.0f, -2.5f),  glm::vec3(1.5f, 2.0f, -2.5f),
                                         glm::vec3(1.5f, 0.2f, -1.5f),   glm::vec3(-1.3f, 1.0f, -1.5f)};

    unsigned int indices[36] = {
        // Back face
        0, 1, 2, 0, 2, 3,
        // Front face
        4, 5, 6, 4, 6, 7,
        // Left face
        8, 9, 10, 8, 10, 11,
        // Right face
        12, 13, 14, 12, 14, 15,
        // Bottom face
        16, 17, 18, 16, 18, 19,
        // Top face
        20, 21, 22, 20, 22, 23};

    unsigned int VBO;

    unsigned int VAO;

    unsigned int EBO;

    unsigned int diffuseId;
    unsigned int specularId;

    unsigned int lightVAO;

    //! TEMP
    Fenrir::Camera& m_camera;
};
#define BIND_WINDOW_SYSTEM_FN(fn, windowInstance) std::bind(&Window::fn, &windowInstance, std::placeholders::_1)

#include <iostream>
int main()
{
    auto logger = std::make_unique<Fenrir::ConsoleLogger>();
    Fenrir::App app(std::move(logger));

    Fenrir::Camera camera;

    CameraController cameraController(camera, 0.1f, 3.0f);

    ShaderLibrary shaderLibrary(*app.Logger().get());
    TextureLibrary textureLibrary(*app.Logger().get());

    Window window(camera, "Demo App");

    app.AddSystems(Fenrir::SchedulePriority::PreInit, {PreInit, BIND_WINDOW_SYSTEM_FN(Window::PreInit, window)})
        .AddSystems(Fenrir::SchedulePriority::Init, {Init})
        .AddSystems(Fenrir::SchedulePriority::PostInit, {PostInit})
        .AddSystems(Fenrir::SchedulePriority::PreUpdate, {systemA})
        .AddSystems(Fenrir::SchedulePriority::Update,
                    {BIND_CAMERA_CONTROLLER_FN(CameraController::Update, cameraController)})
        .AddSystems(Fenrir::SchedulePriority::Tick, {Tick})
        .AddSystems(Fenrir::SchedulePriority::PostUpdate, {BIND_WINDOW_SYSTEM_FN(Window::PostUpdate, window)})
        .AddSystems(Fenrir::SchedulePriority::Exit, {BIND_WINDOW_SYSTEM_FN(Window::Exit, window)})
        .Run();
}