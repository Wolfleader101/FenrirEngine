#pragma once

#include <string>

#include "FenrirMath/Math_fwd.hpp"

namespace Fenrir
{
    class ILogger;
} // namespace Fenrir

class Shader
{
  public:
    /**
     * @brief Construct a new Shader object
     *
     * @param logger the logger to use
     * @param vertexPath the path to the vertex shader
     * @param fragmentPath the path to the fragment shader
     */
    Shader(Fenrir::ILogger* logger, std::string vertexPath, std::string fragmentPath);
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

  private:
    unsigned int Id;
};
