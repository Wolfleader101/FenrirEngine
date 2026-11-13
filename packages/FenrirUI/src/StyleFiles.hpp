#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "FenrirUI/ElementId.hpp"
#include "FenrirUI/Markup.hpp"
#include "FenrirUI/Stylesheet.hpp"
#include "FenrirUI/UI.hpp"

namespace Fenrir
{
    class AssetStore;
    class UIRoot;
    class UIRoots;
} // namespace Fenrir

namespace Fenrir::Detail
{
    /** @brief An error, and the line of the outermost document or sheet it is ordered by. */
    struct OrderedError
    {
        std::uint32_t at = 0;
        MarkupError error;
    };

    using MarkupErrors = std::vector<OrderedError>;

    /**
     * @brief Where a load's documents and sheets come from: `name` turns a src (of an include, a `<style>` or an
     * `@import`) into the name of what it names, from the name of the document or sheet that names it, and `read`
     * gives that name's text, or nothing when there is none.
     */
    struct SourceReader
    {
        std::function<std::string(std::string_view src, std::string_view from)> name;
        std::function<std::optional<std::string>(const std::string& name)> read;
    };

    /** @brief A file a root loaded, and its modification time when it was read; nothing when it had none. */
    struct LoadedFile
    {
        std::filesystem::path path;
        std::optional<std::filesystem::file_time_type> modified;
    };

    /**
     * @brief A reader of files: a src names a path relative to the file that names it (a path relative to the asset
     * root at the top), as a forward-slashed relative path; each file read is added to `files`.
     */
    [[nodiscard]] SourceReader FileReader(std::filesystem::path assetRoot, std::vector<LoadedFile>& files);

    /** @brief A reader through a markup resolver: a src is its own name, and the resolver gives its text. */
    [[nodiscard]] SourceReader ResolverReader(const MarkupResolver& resolver);

    /**
     * @brief Whether any file's modification time now is not the one it was read with. Reads every one of `files`,
     * adding how many to `filesPolled`.
     */
    [[nodiscard]] bool AnyChanged(const std::vector<LoadedFile>& files, std::uint32_t& filesPolled);

    /** @brief Reads each file's modification time again, so the version now on disk is the one compared against. */
    void ReadModifiedTimes(std::vector<LoadedFile>& files);

    /**
     * @brief Parses `text` as the sheet `name`, with each `@import` loaded through `reader` in its place: the
     * imported sheet's rules, keyframes and font faces before the importer's written after the import. An import of a
     * sheet in `loading` (the sheets being loaded, outermost first) is an error and loads nothing. Each `@font-face` is
     * read (ReadFontFace), and one refused is left out. Every error, of the sheet and of its imports, is in `errors`,
     * ordered at its line for the sheet's own and at the import's line for an import's; the sheet has no diagnostics.
     */
    [[nodiscard]] Stylesheet BuildStylesheet(std::string_view text, const std::string& name, const SourceReader& reader,
                                             AssetStore* store, std::vector<std::string>& loading,
                                             MarkupErrors& errors);

    /**
     * @brief Reads a `@font-face` (`font-family`, `src: url()`, `font-weight`, `font-style` and `font-rendering`) and
     * loads its font from the asset store, its `url()` relative to the asset root as every url() of a sheet is.
     * Nothing, with each reason given to `refuse` with its line, for an unknown descriptor, a value it cannot read, a
     * missing family or src, or a font that does not load.
     */
    [[nodiscard]] std::optional<UIFontFace> ReadFontFace(
        const StyleFontFace& face, AssetStore* store,
        const std::function<void(std::uint32_t line, std::string message)>& refuse);

    /** @brief Registers each of the sheet's font faces with `roots` (UIRoots::AddFontFace); errors for each refused. */
    void RegisterFontFaces(UIRoots& roots, AssetStore* store, const Stylesheet& sheet,
                           std::vector<MarkupError>& errors);

    /**
     * @brief A sheet a root loaded from a file, or an inline `<style>` of a document loaded from one that imports
     * files, with the files it read (the sheet's and its imports'), for hot reload.
     */
    struct FileSheet
    {
        StylesheetId id;
        /** @brief The sheet's file, or the document an inline one is in. */
        std::string name;
        /** @brief An inline sheet's CSS, its lines at the document's; empty for a file's. */
        std::string text;
        std::vector<LoadedFile> files;
    };

    /** @brief A markup document a root loaded from a file, with what it made and the files it read (its includes'). */
    struct FileDocument
    {
        std::string name;
        ElementId parent;
        std::vector<ElementId> elements;
        std::vector<StylesheetId> sheets;
        std::vector<LoadedFile> files;
    };

    /** @brief What a load from files read: the document's files, and its sheets that read files of their own. */
    struct FileLoad
    {
        std::vector<LoadedFile> files;
        std::vector<FileSheet> sheets;
    };

    /** @brief A root's hot reload: its switch, the time since its last poll, and the files it loaded. */
    struct HotReloadState
    {
        bool on = false;
        double sincePoll = 0.0;
        /** @brief While a new version of a document loads, its sheets' font faces wait to see whether it is taken. */
        bool holdFontFaces = false;
        std::vector<FileDocument> documents;
        std::vector<FileSheet> sheets;
    };

    /**
     * @brief Loads the markup document `text`, named `name`, under `parent` from its child `index` on (UIRoot::End for
     * after the last), as UIRoot::LoadMarkup does with its sources from `reader`, logging nothing. With `files`, the
     * sheets that read files are in it, and the files read for the document alone stay in its `files`.
     */
    [[nodiscard]] MarkupLoad LoadMarkupDocument(UIRoot& root, const UIRoots& roots, ElementId parent,
                                                std::string_view text, std::string_view name,
                                                const SourceReader& reader, std::size_t index, FileLoad* files);
} // namespace Fenrir::Detail
