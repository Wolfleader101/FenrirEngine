// Markup documents and stylesheets loaded from files, and hot reload: the files a root loaded, polled for changes.
#include <algorithm>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "FenrirApp/App.hpp"
#include "FenrirAssets/AssetStore.hpp"
#include "FenrirUI/UIRoot.hpp"
#include "StyleFiles.hpp"
#include "UIRootState.hpp"

namespace Fenrir
{
    namespace
    {
        /** @brief A reader of files under the app's asset root, adding each file it reads to `files`. */
        Detail::SourceReader AssetFiles(const App& app, std::vector<Detail::LoadedFile>& files)
        {
            const AssetStore* store = app.TryGetResource<AssetStore>();
            return Detail::FileReader(store != nullptr ? store->Root() : std::filesystem::path(), files);
        }

        /** @brief The sheet `text`, named `name`, its imports loaded, with its errors in the order written. */
        Stylesheet BuildSheet(std::string_view text, const std::string& name, const Detail::SourceReader& reader,
                              AssetStore* store, std::vector<MarkupError>& errors)
        {
            Detail::MarkupErrors ordered;
            std::vector<std::string> loading;
            Stylesheet sheet = Detail::BuildStylesheet(text, name, reader, store, loading, ordered);
            for (Detail::OrderedError& error : ordered)
            {
                errors.push_back(std::move(error.error));
            }
            return sheet;
        }

        /** @brief The stylesheet file `name`, as BuildSheet builds it; nothing, with an error, when it is not there. */
        std::optional<Stylesheet> BuildFileSheet(const std::string& name, const Detail::SourceReader& reader,
                                                 AssetStore* store, std::vector<MarkupError>& errors)
        {
            const std::optional<std::string> text = reader.read(name);
            if (!text.has_value())
            {
                errors.push_back({name, 0, "the stylesheet '" + name + "' was not found"});
                return std::nullopt;
            }
            return BuildSheet(*text, name, reader, store, errors);
        }

        /**
         * @brief Hot reload refused a new version of `name`: says so, and reads its files' modification times again,
         * so it is read again only when it next changes.
         */
        void KeepPrevious(const App& app, const std::string& name, std::vector<Detail::LoadedFile>& files)
        {
            app.Logger()->Warn("FenrirUI: {}: hot reload kept the version loaded before", name);
            Detail::ReadModifiedTimes(files);
        }
    } // namespace

    MarkupLoad UIRoot::LoadDocument(ElementId parent, std::string_view path)
    {
        Detail::FileLoad files;
        const Detail::SourceReader reader = AssetFiles(m_state->app, files.files);
        const std::string name = reader.name(path, {});
        MarkupLoad load;
        if (const std::optional<std::string> text = reader.read(name))
        {
            load = Detail::LoadMarkupDocument(*this, m_state->roots, parent, *text, name, reader, End, &files);
            if (IsAlive(parent))
            {
                Detail::HotReloadState& hotReload = m_state->hotReload;
                hotReload.documents.push_back({name, parent, load.elements, load.stylesheets, std::move(files.files)});
                hotReload.sheets.insert(hotReload.sheets.end(), std::make_move_iterator(files.sheets.begin()),
                                        std::make_move_iterator(files.sheets.end()));
            }
        }
        else
        {
            load.errors.push_back({name, 0, "the document '" + name + "' was not found"});
        }
        LogErrors(load.errors);
        return load;
    }

    StylesheetId UIRoot::LoadStylesheet(std::string_view path)
    {
        std::vector<Detail::LoadedFile> files;
        const Detail::SourceReader reader = AssetFiles(m_state->app, files);
        const std::string name = reader.name(path, {});
        std::vector<MarkupError> errors;
        std::optional<Stylesheet> sheet =
            BuildFileSheet(name, reader, m_state->app.TryGetResource<AssetStore>(), errors);
        StylesheetId id;
        if (sheet.has_value())
        {
            id = AttachStylesheet(std::move(*sheet), errors);
            m_state->hotReload.sheets.push_back({id, name, {}, std::move(files)});
        }
        LogErrors(errors);
        return id;
    }

    void UIRoot::SetHotReload(bool on)
    {
        m_state->hotReload.on = on;
        m_state->hotReload.sincePoll = 0.0;
    }

    bool UIRoot::IsHotReloadOn() const noexcept
    {
        return m_state->hotReload.on;
    }

    void UIRoot::PollFiles(double deltaTime)
    {
        Detail::HotReloadState& hotReload = m_state->hotReload;
        if (!hotReload.on)
        {
            return;
        }
        hotReload.sincePoll += deltaTime;
        if (hotReload.sincePoll < HotReloadInterval)
        {
            return;
        }
        // Polls keep to the interval's beat; a frame longer than it owes no second poll.
        hotReload.sincePoll -= HotReloadInterval;
        if (hotReload.sincePoll >= HotReloadInterval)
        {
            hotReload.sincePoll = 0.0;
        }
        std::uint32_t& polled = m_state->stats.filesPolled;
        for (std::size_t i = 0; i < hotReload.documents.size();)
        {
            const Detail::FileDocument& document = hotReload.documents[i];
            const bool anyAlive = std::any_of(document.elements.begin(), document.elements.end(),
                                              [&](ElementId element) { return IsAlive(element); });
            if (!IsAlive(document.parent) || (!document.elements.empty() && !anyAlive))
            {
                // The game took the document away: it is not loaded back.
                hotReload.documents.erase(hotReload.documents.begin() + std::ptrdiff_t(i));
                continue;
            }
            if (Detail::AnyChanged(document.files, polled))
            {
                ReloadDocument(i);
            }
            ++i;
        }
        // A reloaded document's sheets were replaced, and are dropped here, unpolled, as removed ones are.
        for (std::size_t i = 0; i < hotReload.sheets.size();)
        {
            if (GetStylesheet(hotReload.sheets[i].id) == nullptr)
            {
                hotReload.sheets.erase(hotReload.sheets.begin() + std::ptrdiff_t(i));
                continue;
            }
            if (Detail::AnyChanged(hotReload.sheets[i].files, polled))
            {
                ReloadStylesheet(i);
            }
            ++i;
        }
    }

    void UIRoot::ReloadDocument(std::size_t index)
    {
        Detail::HotReloadState& hotReload = m_state->hotReload;
        Detail::FileDocument& document = hotReload.documents[index];
        // The new elements go where the old ones start, so they keep their place among the parent's siblings.
        std::size_t at = End;
        const std::span<const ElementId> children = GetChildren(document.parent);
        for (std::size_t child = 0; child < children.size(); ++child)
        {
            if (std::find(document.elements.begin(), document.elements.end(), children[child]) !=
                document.elements.end())
            {
                at = child;
                break;
            }
        }

        Detail::FileLoad files;
        const Detail::SourceReader reader = AssetFiles(m_state->app, files.files);
        MarkupLoad load;
        if (const std::optional<std::string> text = reader.read(document.name))
        {
            // The font family registry is every root's: the new version's faces wait until it is taken.
            hotReload.holdFontFaces = true;
            load = Detail::LoadMarkupDocument(*this, m_state->roots, document.parent, *text, document.name, reader, at,
                                              &files);
            hotReload.holdFontFaces = false;
        }
        else
        {
            load.errors.push_back({document.name, 0, "the document '" + document.name + "' was not found"});
        }

        if (!load.errors.empty())
        {
            for (const ElementId element : load.elements)
            {
                Remove(element, RemoveMode::Immediate);
            }
            for (const StylesheetId sheet : load.stylesheets)
            {
                RemoveStylesheet(sheet);
            }
            LogErrors(load.errors);
            KeepPrevious(m_state->app, document.name, document.files);
            return;
        }

        for (const ElementId element : document.elements)
        {
            Remove(element, RemoveMode::Immediate);
        }
        for (const StylesheetId sheet : document.sheets)
        {
            RemoveStylesheet(sheet);
        }
        std::vector<MarkupError> faceErrors;
        for (const StylesheetId sheet : load.stylesheets)
        {
            Detail::RegisterFontFaces(m_state->roots, m_state->app.TryGetResource<AssetStore>(), *GetStylesheet(sheet),
                                      faceErrors);
        }
        LogErrors(faceErrors);
        const ElementId parent = document.parent;
        document.elements = load.elements;
        document.sheets = load.stylesheets;
        document.files = std::move(files.files);
        hotReload.sheets.insert(hotReload.sheets.end(), std::make_move_iterator(files.sheets.begin()),
                                std::make_move_iterator(files.sheets.end()));

        UIEvent event;
        event.target = load.element.IsValid() ? load.element : parent;
        event.kind = UIEventKind::DocumentReloaded;
        SendEvent(event);
    }

    void UIRoot::ReloadStylesheet(std::size_t index)
    {
        Detail::FileSheet& sheet = m_state->hotReload.sheets[index];
        std::vector<Detail::LoadedFile> files;
        const Detail::SourceReader reader = AssetFiles(m_state->app, files);
        AssetStore* store = m_state->app.TryGetResource<AssetStore>();
        std::vector<MarkupError> errors;
        std::optional<Stylesheet> built = sheet.text.empty()
                                              ? BuildFileSheet(sheet.name, reader, store, errors)
                                              : BuildSheet(sheet.text, sheet.name, reader, store, errors);
        if (!errors.empty() || !built.has_value())
        {
            LogErrors(errors);
            KeepPrevious(m_state->app, sheet.name, sheet.files);
            return;
        }
        std::vector<MarkupError> faceErrors;
        ReplaceStylesheet(sheet.id, std::move(*built), faceErrors);
        LogErrors(faceErrors);
        sheet.files = std::move(files);
    }
} // namespace Fenrir
