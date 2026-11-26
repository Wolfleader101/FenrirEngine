#include "FenrirAssets/Assets.hpp"

#include <cassert>
#include <utility>

#include "FenrirApp/App.hpp"
#include "FenrirAssets/Generated/FenrirAssetsTypes.hpp"
#include "FenrirTypes/TypeRegistry.hpp"

namespace Fenrir
{
    Assets Assets::Install(App& app, AssetsConfig config)
    {
        // Its types first (ADR-0074), before any resource or system: a second Install registers nothing again.
        Generated::RegisterFenrirAssetsTypes(app.GetResource<TypeRegistry>());

        // The store reads data asset types from the app's registry, which outlives it.
        const bool hotReload = config.hotReload;
        app.AddResource<AssetStore>(*app.Logger(), std::move(config), app.GetResource<TypeRegistry>());
        // The watcher looks before anything else runs in a frame, on the main thread, so a reload happens between
        // frames (ADR-0081 §1). It finds the store each frame: one this owner removed reloads nothing.
        if (hotReload)
        {
            app.AddSequentialSystems(SchedulePriority::PreUpdate, {[](App& running) {
                                         if (AssetStore* store = running.TryGetResource<AssetStore>())
                                         {
                                             store->PollForReload();
                                         }
                                     }});
        }
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
