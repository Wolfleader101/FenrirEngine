#include "ShaderLibrary.hpp"

#include <glad/glad.h>

#include "FenrirLogger/ILogger.hpp"
#include "FenrirMath/Math.hpp"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

Shader::Shader(unsigned int id) : Id(id)
{
}

Shader::~Shader()
{
    glDeleteProgram(Id);
}

void Shader::Use()
{
    glUseProgram(Id);
}

void Shader::SetBool(const std::string& name, bool value) const
{
    glUniform1i(glGetUniformLocation(Id, name.c_str()), static_cast<int>(value));
}

void Shader::SetInt(const std::string& name, int value) const
{
    glUniform1i(glGetUniformLocation(Id, name.c_str()), value);
}

void Shader::SetFloat(const std::string& name, float value) const
{
    glUniform1f(glGetUniformLocation(Id, name.c_str()), value);
}

void Shader::SetVec2(const std::string& name, const Fenrir::Math::Vec2& value) const
{
    glUniform2fv(glGetUniformLocation(Id, name.c_str()), 1, Fenrir::Math::AsArray(value));
}

void Shader::SetVec2(const std::string& name, float x, float y) const
{
    glUniform2f(glGetUniformLocation(Id, name.c_str()), x, y);
}

void Shader::SetVec3(const std::string& name, const Fenrir::Math::Vec3& value) const
{
    glUniform3fv(glGetUniformLocation(Id, name.c_str()), 1, Fenrir::Math::AsArray(value));
}

void Shader::SetVec3(const std::string& name, float x, float y, float z) const
{
    glUniform3f(glGetUniformLocation(Id, name.c_str()), x, y, z);
}

void Shader::SetVec4(const std::string& name, const Fenrir::Math::Vec4& value) const
{
    glUniform4fv(glGetUniformLocation(Id, name.c_str()), 1, Fenrir::Math::AsArray(value));
}

void Shader::SetVec4(const std::string& name, float x, float y, float z, float w) const
{
    glUniform4f(glGetUniformLocation(Id, name.c_str()), x, y, z, w);
}

void Shader::SetMat3(const std::string& name, const Fenrir::Math::Mat3& mat) const
{
    glUniformMatrix3fv(glGetUniformLocation(Id, name.c_str()), 1, GL_FALSE, Fenrir::Math::AsArray(mat));
}

void Shader::SetMat4(const std::string& name, const Fenrir::Math::Mat4& mat) const
{
    glUniformMatrix4fv(glGetUniformLocation(Id, name.c_str()), 1, GL_FALSE, Fenrir::Math::AsArray(mat));
}

ShaderLibrary::ShaderLibrary(Fenrir::ILogger& logger) : m_logger(logger)
{
    m_shaders["error"] = LoadShader("assets/shaders/error.vert", "assets/shaders/error.frag");
}

void ShaderLibrary::AddShader(const std::string& name, const std::string& vertexPath, const std::string& fragmentPath)
{
    if (m_shaders.find(name) != m_shaders.end())
    {
        m_logger.Warn("ShaderLibrary::AddShader - Shader already loaded: ", name);
        return;
    }

    m_shaders[name] = LoadShader(vertexPath, fragmentPath);
}

const Shader& ShaderLibrary::GetShader(const std::string& name) const
{
    if (m_shaders.find(name) == m_shaders.end())
    {
        m_logger.Fatal("ShaderLibrary::GetShader - SHADER not loaded: ", name);

        return m_shaders.at("error");
    }

    return m_shaders.at(name);
}

bool ShaderLibrary::HasShader(const std::string& name) const
{
    return m_shaders.find(name) != m_shaders.end();
}

Shader ShaderLibrary::LoadShader(const std::string& vertexPath, const std::string& fragmentPath)
{
    // TODO this could be moved to some form of file manager
    // TODO would be nice to have reference to renderer here so it doesnt call GL specific code
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
        m_logger.Fatal("COULD NOT READ SHADERS {0} {1}, message: {2}", vertexPath, fragmentPath, e.what());
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
        m_logger.Fatal("VERTEX SHADER COMPILATION FAILED\n{0}", infoLog);
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
        m_logger.Fatal("FRAGMENT SHADER COMPILATION FAILED\n{0}", infoLog);
    }

    Shader shader;

    // create a shader program object, store its ID in Id
    shader.Id = glCreateProgram();

    // attach the vertex shader and fragment shader to the shader program
    glAttachShader(shader.Id, vertex);
    glAttachShader(shader.Id, fragment);

    // link the shader program
    glLinkProgram(shader.Id);

    // check for linking errors
    glGetProgramiv(shader.Id, GL_LINK_STATUS, &success);
    if (!success)
    {
        glGetProgramInfoLog(shader.Id, 512, nullptr, infoLog);
        m_logger.Fatal("SHADER PROGRAM LINKING FAILED\n{0}", infoLog);
    }

    // delete the shaders as they have been linked and not needed
    glDeleteShader(vertex);
    glDeleteShader(fragment);

    return shader;
}