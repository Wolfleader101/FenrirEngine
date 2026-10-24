#include "FenrirAssets/Assets.hpp"

#include <cassert>
#include <utility>

#include "FenrirApp/App.hpp"

namespace Fenrir
{
    Assets Assets::Install(App& app, AssetsConfig config)
    {
        app.AddResource<AssetStore>(*app.Logger(), std::move(config));
        return Assets(app);
    }

    Assets::Assets(App& app) noexcept : m_app(&app)
    {
    }

    Assets::Assets(Assets&& other) noexcept : m_app(std::exchange(other.m_app, nullptr))
    {
    }

    Assets& Assets::operator=(Assets&& other) noexcept
    {
        if (this != &other)
        {
            Shutdown();
            m_app = std::exchange(other.m_app, nullptr);
        }
        return *this;
    }

    Assets::~Assets()
    {
        Shutdown();
    }

    bool Assets::IsValid() const noexcept
    {
        return m_app != nullptr;
    }

    AssetStore& Assets::Store() const
    {
        assert(m_app != nullptr && "Assets::Store: the owner was moved from");
        return m_app->GetResource<AssetStore>();
    }

    void Assets::Shutdown() noexcept
    {
        if (m_app != nullptr)
        {
            m_app->RemoveResource<AssetStore>();
            m_app = nullptr;
        }
    }
} // namespace Fenrir
