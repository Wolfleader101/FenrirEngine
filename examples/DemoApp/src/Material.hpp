#pragma once

#include <string>
#include <variant>

#include <entt/container/dense_map.hpp>

#include "FenrirMath/Math.hpp"

#include "ShaderLibrary.hpp"

using MaterialProperty = std::variant<bool, int, float, Fenrir::Math::Vec2, Fenrir::Math::Vec3, Fenrir::Math::Vec4,
                                      Fenrir::Math::Mat3, Fenrir::Math::Mat4>;

struct Material
{
    Shader shader;
    entt::dense_map<std::string, MaterialProperty> properties;
};