#include "FenrirPainter/Painting.hpp"

#include <span>
#include <utility>

#include "FenrirApp/App.hpp"
#include "FenrirAssets/AssetStore.hpp"
#include "FenrirAssets/ShaderData.hpp"
#include "FenrirShaders/BuiltinPainter.hpp"

namespace Fenrir
{
    Painting Painting::Install(App& app)
    {
        AssetStore* store = app.TryGetResource<AssetStore>();
        if (store == nullptr)
        {
            app.Logger()->Error("FenrirPainter: there is no asset store to register builtin:painter with; install "
                                "FenrirAssets first");
            return Painting({});
        }
        ShaderData shader;
        const auto bytes = std::as_bytes(std::span(Shaders::BuiltinPainter));
        shader.bytes.assign(bytes.begin(), bytes.end());
        // The store logs why it refuses a shader.
        return Painting(store->AddBuiltin<Fenrir::Shader>("builtin:painter", std::move(shader)));
    }

    Painting::Painting(AssetHandle<Fenrir::Shader> shader) noexcept : m_shader(shader)
    {
    }

    Painting::Painting(Painting&& other) noexcept : m_shader(std::exchange(other.m_shader, {}))
    {
    }

    Painting& Painting::operator=(Painting&& other) noexcept
    {
        m_shader = std::exchange(other.m_shader, {});
        return *this;
    }

    bool Painting::IsValid() const noexcept
    {
        return !m_shader.IsEmpty();
    }

    AssetHandle<Fenrir::Shader> Painting::GetShader() const noexcept
    {
        return m_shader;
    }
} // namespace Fenrir
