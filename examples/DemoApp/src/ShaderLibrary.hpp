#pragma once

#include <string>
#include <unordered_map>

#include "FenrirMath/Math_fwd.hpp"

namespace Fenrir
{
    class ILogger;
} // namespace Fenrir

/**
 * @brief a simple wrapper around a shader program which is just an id
 *
 */
class Shader
{
  public:
    Shader() = default;

    Shader(unsigned int id);

    unsigned int Id;

    ~Shader();
    /**
     * @brief use the shader program
     *
     */
    void Use();
    void SetBool(const std::string& name, bool value) const;
    void SetInt(const std::string& name, int value) const;
    void SetFloat(const std::string& name, float value) const;
    void SetVec2(const std::string& name, const Fenrir::Math::Vec2& value) const;
    void SetVec2(const std::string& name, float x, float y) const;
    void SetVec3(const std::string& name, const Fenrir::Math::Vec3& value) const;
    void SetVec3(const std::string& name, float x, float y, float z) const;
    void SetVec4(const std::string& name, const Fenrir::Math::Vec4& value) const;
    void SetVec4(const std::string& name, float x, float y, float z, float w) const;
    void SetMat3(const std::string& name, const Fenrir::Math::Mat3& mat) const;
    void SetMat4(const std::string& name, const Fenrir::Math::Mat4& mat) const;
};

class ShaderLibrary
{
  public:
    /**
     * @brief Construct a new Shader Library object
     *
     * @param logger logger to be injected
     */
    ShaderLibrary(Fenrir::ILogger& logger);

    /**
     * @brief Add a shader to the library
     *
     * @param name of the shader
     * @param vertexPath path to the vertex shader
     * @param fragmentPath path to the fragment shader
     */
    void AddShader(const std::string& name, const std::string& vertexPath, const std::string& fragmentPath);

    /**
     * @brief Get the Shader object
     *
     * @param name shader to get
     * @return Shader& shader object
     */
    const Shader& GetShader(const std::string& name) const;

    /**
     * @brief Check if the library has a shader
     *
     * @param name shader to check for
     * @return true if the library has the shader
     * @return false if the library does not have the shader
     */
    bool HasShader(const std::string& name) const;

  private:
    Fenrir::ILogger& m_logger;

    std::unordered_map<std::string, Shader> m_shaders;

    Shader LoadShader(const std::string& vertexPath, const std::string& fragmentPath);
};
