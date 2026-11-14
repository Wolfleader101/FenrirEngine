#pragma once

#include <filesystem>
#include <system_error>

#include "FenrirAssets/AssetStore.hpp"

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
} // namespace UIDemo
