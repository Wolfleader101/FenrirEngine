#include "StyleFiles.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <sstream>
#include <system_error>
#include <utility>

#include "FenrirAssets/AssetStore.hpp"
#include "FenrirUI/Style.hpp"
#include "StyleValues.hpp"

namespace Fenrir::Detail
{
    namespace
    {
        std::filesystem::path Utf8Path(std::string_view text)
        {
            return std::filesystem::path(
                std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size())); // NOLINT
        }

        std::string Utf8Text(const std::filesystem::path& path)
        {
            const std::u8string text = path.generic_u8string();
            return {reinterpret_cast<const char*>(text.data()), text.size()}; // NOLINT
        }

        std::optional<std::filesystem::file_time_type> ModifiedTime(const std::filesystem::path& path)
        {
            std::error_code error;
            const std::filesystem::file_time_type time = std::filesystem::last_write_time(path, error);
            if (error)
            {
                return std::nullopt;
            }
            return time;
        }

        /** @brief `text` without one pair of matching quotes around it. */
        std::string_view Unquoted(std::string_view text)
        {
            if (text.size() >= 2 && (text.front() == '"' || text.front() == '\'') && text.back() == text.front())
            {
                return text.substr(1, text.size() - 2);
            }
            return text;
        }

        /** @brief The path of a `src` descriptor's first `url()`, which may be followed by a `format()`. */
        std::optional<std::string> FontSource(std::string_view value)
        {
            value = Trim(value);
            if (value.size() < 5 || Lowered(value.substr(0, 4)) != "url(")
            {
                return std::nullopt;
            }
            const std::size_t close = value.find(')');
            if (close == std::string_view::npos)
            {
                return std::nullopt;
            }
            const std::string_view rest = Trim(value.substr(close + 1));
            if (!rest.empty() && Lowered(rest.substr(0, std::min<std::size_t>(rest.size(), 7))) != "format(")
            {
                return std::nullopt;
            }
            const std::string_view path = Unquoted(Trim(value.substr(4, close - 4)));
            if (path.empty())
            {
                return std::nullopt;
            }
            return std::string(path);
        }

        /** @brief Appends `from`'s items written before `line`, from `next` on, to `to`. */
        template <typename T>
        void TakeBefore(std::vector<T>& from, std::size_t& next, std::uint32_t line, std::vector<T>& to)
        {
            while (next < from.size() && from[next].line < line)
            {
                to.push_back(std::move(from[next]));
                ++next;
            }
        }

        template <typename T>
        void Append(std::vector<T>& from, std::vector<T>& to)
        {
            to.insert(to.end(), std::make_move_iterator(from.begin()), std::make_move_iterator(from.end()));
        }
    } // namespace

    SourceReader FileReader(std::filesystem::path assetRoot, std::vector<LoadedFile>& files)
    {
        SourceReader reader;
        reader.name = [](std::string_view src, std::string_view from) {
            const std::filesystem::path path = Utf8Path(src);
            if (path.is_absolute() || from.empty())
            {
                return Utf8Text(path.lexically_normal());
            }
            return Utf8Text((Utf8Path(from).parent_path() / path).lexically_normal());
        };
        reader.read = [root = std::move(assetRoot), &files](const std::string& name) -> std::optional<std::string> {
            const std::filesystem::path relative = Utf8Path(name);
            const std::filesystem::path path = relative.is_absolute() ? relative : root / relative;
            // The time is taken before the read, so a write during it is seen by the next poll.
            const std::optional<std::filesystem::file_time_type> modified = ModifiedTime(path);
            std::ifstream in(path, std::ios::binary);
            if (!modified.has_value() || !in)
            {
                return std::nullopt;
            }
            std::ostringstream text;
            text << in.rdbuf();
            files.push_back({path, modified});
            return std::move(text).str();
        };
        return reader;
    }

    SourceReader ResolverReader(const MarkupResolver& resolver)
    {
        SourceReader reader;
        reader.name = [](std::string_view src, std::string_view) { return std::string(src); };
        reader.read = [&resolver](const std::string& name) -> std::optional<std::string> {
            return resolver ? resolver(name) : std::nullopt;
        };
        return reader;
    }

    bool AnyChanged(const std::vector<LoadedFile>& files, std::uint32_t& polled)
    {
        bool changed = false;
        for (const LoadedFile& file : files)
        {
            ++polled;
            changed = changed || ModifiedTime(file.path) != file.modified;
        }
        return changed;
    }

    void TakeTimes(std::vector<LoadedFile>& files)
    {
        for (LoadedFile& file : files)
        {
            file.modified = ModifiedTime(file.path);
        }
    }

    Stylesheet BuildStylesheet(std::string_view text, const std::string& name, const SourceReader& reader,
                               AssetStore* store, std::vector<std::string>& loading, MarkupErrors& errors)
    {
        Stylesheet sheet = Stylesheet::Parse(text, name);
        const auto error = [&](std::uint32_t line, std::string message) {
            errors.push_back({line, MarkupError{name, line, std::move(message)}});
        };
        for (StyleDiagnostic& diagnostic : sheet.diagnostics)
        {
            error(diagnostic.line, std::move(diagnostic.message));
        }
        sheet.diagnostics.clear();
        std::erase_if(sheet.fontFaces,
                      [&](const StyleFontFace& face) { return !ReadFontFace(face, store, error).has_value(); });
        if (sheet.imports.empty())
        {
            return sheet;
        }

        loading.push_back(name);
        Stylesheet merged;
        merged.name = sheet.name;
        merged.imports = sheet.imports;
        std::size_t rule = 0;
        std::size_t keyframes = 0;
        std::size_t face = 0;
        for (const StyleImport& import : sheet.imports)
        {
            TakeBefore(sheet.rules, rule, import.line, merged.rules);
            TakeBefore(sheet.keyframes, keyframes, import.line, merged.keyframes);
            TakeBefore(sheet.fontFaces, face, import.line, merged.fontFaces);
            const std::string imported = reader.name(import.url, name);
            if (std::find(loading.begin(), loading.end(), imported) != loading.end())
            {
                error(import.line, "the @import of '" + import.url + "' loops: it is being loaded");
                continue;
            }
            std::optional<std::string> importedText = reader.read(imported);
            if (!importedText.has_value())
            {
                error(import.line, "the stylesheet '" + import.url + "' was not found");
                continue;
            }
            MarkupErrors importErrors;
            Stylesheet importedSheet = BuildStylesheet(*importedText, imported, reader, store, loading, importErrors);
            for (OrderedError& importError : importErrors)
            {
                errors.push_back({import.line, std::move(importError.error)});
            }
            Append(importedSheet.rules, merged.rules);
            Append(importedSheet.keyframes, merged.keyframes);
            Append(importedSheet.fontFaces, merged.fontFaces);
        }
        loading.pop_back();
        merged.rules.insert(merged.rules.end(), std::make_move_iterator(sheet.rules.begin() + std::ptrdiff_t(rule)),
                            std::make_move_iterator(sheet.rules.end()));
        merged.keyframes.insert(merged.keyframes.end(),
                                std::make_move_iterator(sheet.keyframes.begin() + std::ptrdiff_t(keyframes)),
                                std::make_move_iterator(sheet.keyframes.end()));
        merged.fontFaces.insert(merged.fontFaces.end(),
                                std::make_move_iterator(sheet.fontFaces.begin() + std::ptrdiff_t(face)),
                                std::make_move_iterator(sheet.fontFaces.end()));
        return merged;
    }

    std::optional<UIFontFace> ReadFontFace(const StyleFontFace& face, AssetStore* store,
                                           const std::function<void(std::uint32_t line, std::string message)>& refuse)
    {
        UIFontFace read;
        std::optional<std::string> source;
        std::uint32_t sourceLine = face.line;
        bool refused = false;
        for (const StyleDeclaration& descriptor : face.descriptors)
        {
            const std::string written = descriptor.name + ": " + descriptor.value;
            if (descriptor.name == "font-family" || descriptor.name == "font-weight" || descriptor.name == "font-style")
            {
                // The same values as the properties of the same names.
                StyleDeclarations declarations;
                StyleParseContext context;
                context.checkOnly = true;
                if (!ParseStyleProperty(descriptor.name, descriptor.value, declarations, context))
                {
                    refuse(descriptor.line, "@font-face's '" + written + "' was refused: " + context.error);
                    refused = true;
                }
                else if (descriptor.name == "font-family")
                {
                    read.family = declarations.Values().fontFamily;
                }
                else if (descriptor.name == "font-weight")
                {
                    read.weight = declarations.Values().fontWeight;
                }
                else
                {
                    read.style = declarations.Values().fontStyle;
                }
            }
            else if (descriptor.name == "src")
            {
                source = FontSource(descriptor.value);
                sourceLine = descriptor.line;
                if (!source.has_value())
                {
                    refuse(descriptor.line, "@font-face's '" + written + "' was refused: it is not url(path)");
                    refused = true;
                }
            }
            else if (descriptor.name == "font-rendering")
            {
                const std::string value = Lowered(Trim(descriptor.value));
                if (value == "bitmap" || value == "sdf")
                {
                    read.rendering = value == "sdf" ? FontRendering::Sdf : FontRendering::Bitmap;
                }
                else
                {
                    refuse(descriptor.line, "@font-face's '" + written + "' was refused: it is not bitmap or sdf");
                    refused = true;
                }
            }
            else
            {
                refuse(descriptor.line, "@font-face has no descriptor '" + descriptor.name + "'");
                refused = true;
            }
        }
        if (refused)
        {
            return std::nullopt;
        }
        if (read.family.empty() || !source.has_value())
        {
            refuse(face.line, read.family.empty() ? "@font-face has no font-family" : "@font-face has no src");
            return std::nullopt;
        }
        if (store == nullptr)
        {
            refuse(sourceLine, "@font-face's font '" + *source + "' was not loaded: there is no asset store");
            return std::nullopt;
        }
        const LoadResult<Font> loaded = store->Load<Font>(*source, AssetOwner::App());
        if (loaded.handle.IsEmpty())
        {
            refuse(sourceLine, "@font-face's font '" + *source + "' was not loaded");
            return std::nullopt;
        }
        read.font = loaded.handle;
        return read;
    }

    void RegisterFontFaces(UIRoots& roots, AssetStore* store, const Stylesheet& sheet, std::vector<MarkupError>& errors)
    {
        const auto refuse = [&](std::uint32_t line, std::string message) {
            errors.push_back({sheet.name, line, std::move(message)});
        };
        for (const StyleFontFace& face : sheet.fontFaces)
        {
            if (std::optional<UIFontFace> read = ReadFontFace(face, store, refuse))
            {
                if (!roots.AddFontFace(*read))
                {
                    refuse(face.line, "@font-face's family '" + read->family +
                                          "' was not registered: the app has no font library");
                }
            }
        }
    }
} // namespace Fenrir::Detail
