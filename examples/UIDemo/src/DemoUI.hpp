#pragma once

#include <filesystem>
#include <system_error>

#include "FenrirAssets/AssetStore.hpp"
#include "FenrirUI/UIRoot.hpp"

// What the HUD's and the menu's sources share: where their files are read from, and their elements' behaviours.
namespace UIDemo
{
    /**
     * @brief Where the UI's files are read from: the source tree's examples/UIDemo/assets while `probe` (a file
     * relative to it) is there, so an edit there is what hot reload shows and a rebuild keeps, and the asset root
     * otherwise. UIDEMO_SOURCE_ASSETS is UIDemoCore's own definition.
     */
    inline std::filesystem::path UIFileRoot(const Fenrir::AssetStore& store, const char* probe)
    {
#ifdef UIDEMO_SOURCE_ASSETS
        const std::filesystem::path source{UIDEMO_SOURCE_ASSETS};
        std::error_code error;
        if (std::filesystem::exists(source / probe, error))
        {
            return source;
        }
#else
        (void)probe;
#endif
        return store.Root();
    }

    /** @brief `element`'s behaviour as a `T`, or null when it is stale or of another class. */
    template <typename T>
    T* As(const Fenrir::UIRoot& root, Fenrir::ElementId element)
    {
        return dynamic_cast<T*>(root.GetElement(element));
    }
} // namespace UIDemo
