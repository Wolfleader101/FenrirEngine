#include "FenrirMoveAsset/MoveAssetMain.hpp"

#include <filesystem>
#include <iostream>
#include <optional>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "FenrirApp/Generated/FenrirAppTypes.hpp"
#include "FenrirAssets/Assets.hpp"
#include "FenrirAssets/Generated/FenrirAssetsTypes.hpp"
#include "FenrirAssets/Move.hpp"
#include "FenrirECS/Generated/FenrirECSTypes.hpp"
#include "FenrirMath/Generated/FenrirMathTypes.hpp"
#include "FenrirTypes/TypeRegistry.hpp"

namespace Fenrir
{
    namespace
    {
        // The usage, under the name the program was run by: the engine's copy and a game's are named differently.
        void PrintUsage(std::ostream& stream, const char* program)
        {
            const std::string name =
                program != nullptr ? std::filesystem::path(program).stem().string() : "fenrir-move-asset";
            stream << "usage: " << name << " <root> <from> <to> [--dry-run]\n"
                   << "  <root>      the asset root\n"
                   << "  <from>      the asset path of the file to move, under the root\n"
                   << "  <to>        its new asset path, which must name nothing yet\n"
                   << "  --dry-run   write nothing; print what would change\n"
                   << "  --help, -h  print this\n";
        }

        struct Arguments
        {
            std::string_view root;
            std::string_view from;
            std::string_view to;
            bool dryRun = false;
        };

        // The arguments, or nothing when they are not three paths and an optional --dry-run.
        std::optional<Arguments> Parse(std::span<const char* const> args)
        {
            std::vector<std::string_view> paths;
            Arguments arguments;
            for (const char* arg : args)
            {
                const std::string_view text(arg);
                if (text == "--dry-run")
                {
                    arguments.dryRun = true;
                }
                else if (text.starts_with("--"))
                {
                    return std::nullopt;
                }
                else
                {
                    paths.push_back(text);
                }
            }
            if (paths.size() != 3)
            {
                return std::nullopt;
            }
            arguments.root = paths[0];
            arguments.from = paths[1];
            arguments.to = paths[2];
            return arguments;
        }

        void PrintFiles(const MoveResult& result)
        {
            for (const std::string& path : result.changed)
            {
                std::cout << "changed: " << path << '\n';
            }
            for (const UnscannedFile& file : result.unscanned)
            {
                std::cout << "unscanned: " << file.path << ": " << file.reason << '\n';
            }
        }
    } // namespace

    int MoveAssetMain(int argc, const char* const* argv, std::initializer_list<TypeRegistration> registrations)
    {
        const std::span<const char* const> args(argv + 1, argc > 1 ? static_cast<std::size_t>(argc - 1) : 0);
        for (const char* arg : args)
        {
            if (std::string_view(arg) == "--help" || std::string_view(arg) == "-h")
            {
                PrintUsage(std::cout, argc > 0 ? argv[0] : nullptr);
                return 0;
            }
        }
        const std::optional<Arguments> arguments = Parse(args);
        if (!arguments)
        {
            PrintUsage(std::cerr, argc > 0 ? argv[0] : nullptr);
            return 2;
        }

        // As App's constructor and Assets::Install fill the app's registry, then the game's on top.
        TypeRegistry registry([](std::string_view message) { std::cerr << "type registry: " << message << '\n'; });
        // A registry missing types would leave their files unscanned, and so their references to the old path: stop.
        bool registered = Generated::RegisterFenrirMathTypes(registry);
        registered = Generated::RegisterFenrirECSTypes(registry) && registered;
        registered = Generated::RegisterFenrirAppTypes(registry) && registered;
        registered = Generated::RegisterFenrirAssetsTypes(registry) && registered;
        for (TypeRegistration registration : registrations)
        {
            registered = registration(registry) && registered;
        }
        if (!registered)
        {
            std::cerr << "refused: a type registration failed, so nothing was moved\n";
            return 1;
        }

        const MoveResult result = Assets::Move(std::filesystem::path(arguments->root), arguments->from, arguments->to,
                                               registry, arguments->dryRun);
        switch (result.status)
        {
        case MoveStatus::Succeeded:
            std::cout << (arguments->dryRun ? "would move " : "moved ") << arguments->from << " to " << arguments->to
                      << ", rewriting " << result.references << " references in " << result.changed.size()
                      << " files\n";
            PrintFiles(result);
            return 0;
        case MoveStatus::Refused:
            std::cerr << "refused: " << result.problem << '\n';
            return 1;
        case MoveStatus::Failed:
            std::cerr << "failed: " << result.problem << '\n';
            PrintFiles(result);
            return 3;
        }
        return 3;
    }
} // namespace Fenrir
