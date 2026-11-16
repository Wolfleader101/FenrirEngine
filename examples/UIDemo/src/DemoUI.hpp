#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>

#include "FenrirApp/App.hpp"
#include "FenrirAssets/AssetStore.hpp"
#include "FenrirUI/Markup.hpp"
#include "FenrirUI/UIRoot.hpp"

// What the HUD's, the menu's and the world roots' sources share: where their files are read from, and their elements'
// behaviours.
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

    /**
     * @brief Loads the stylesheet at `path` into `root`, logging an error when it does not load or has problems.
     * FenrirUI logs each problem as a warning; a demo UI missing a part is an error, so a smoke run fails on it.
     * `whose` names the UI in the message ("the HUD's").
     */
    inline Fenrir::StylesheetId LoadStylesheetReporting(Fenrir::App& app, Fenrir::UIRoot& root, const std::string& path,
                                                        std::string_view whose)
    {
        const Fenrir::StylesheetId stylesheet = root.LoadStylesheet(path);
        if (!stylesheet.IsValid())
        {
            app.Logger()->Error("UIDemo: {} stylesheet {} did not load", whose, path);
        }
        else if (const Fenrir::Stylesheet* sheet = root.GetStylesheet(stylesheet);
                 sheet != nullptr && !sheet->diagnostics.empty())
        {
            app.Logger()->Error("UIDemo: {} stylesheet {} has {} problems, logged above", whose, path,
                                sheet->diagnostics.size());
        }
        return stylesheet;
    }

    /**
     * @brief Loads the markup document at `path` under `parent`, logging an error when it loads with problems, as
     * LoadStylesheetReporting does for a stylesheet.
     */
    inline Fenrir::MarkupLoad LoadDocumentReporting(Fenrir::App& app, Fenrir::UIRoot& root, Fenrir::ElementId parent,
                                                    const std::string& path, std::string_view whose)
    {
        Fenrir::MarkupLoad load = root.LoadDocument(parent, path);
        if (!load.element.IsValid() || !load.errors.empty())
        {
            app.Logger()->Error("UIDemo: {} document {} loaded with {} problems, logged above", whose, path,
                                load.errors.size());
        }
        return load;
    }

    /** @brief `element`'s behaviour as a `T`, or null when it is stale or of another class. */
    template <typename T>
    T* As(const Fenrir::UIRoot& root, Fenrir::ElementId element)
    {
        return dynamic_cast<T*>(root.GetElement(element));
    }
} // namespace UIDemo
