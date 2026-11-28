# FenrirSerialisation

The value tree, its JSON5 text, its tree encoding and the bridge between trees and described values (ADR-0077, ADR-0079, ADR-0083 §2, ADR-0085 §2). It knows no files, assets, scenes or envelope: it links `FenrirTypes` and `FenrirLogger` alone, and everything is in namespace `Fenrir::Serialisation`. `FenrirAssets` builds the data asset and scene files on it, and a game's own files, such as its save files, compose the same doors without `FenrirAssets` (ADR-0079).

## What it offers

- **`Value`** (`Value.hpp`) is a node of a value tree: an object, array, string, number in its own width, bool or null, with the location it was read from and the comments around it as trivia. `Equals(a, b, Trivia)` compares two trees, with or without their comments.
- **`ReadText(text, path, logger)` and `WriteText(tree)`** (`Text.hpp`) read the full JSON5 grammar and write the fixed subset the engine writes, comments where the tree holds them, the same bytes on every platform.
- **`EncodeTree(tree, Trivia)` and `DecodeTree(bytes, path, logger)`** (`Encoding.hpp`) are the tree's compact binary form, for the C# side and for cooked files. It starts with a zero byte.
- **`ReadTree(bytes, path, logger)`** (`ReadTree.hpp`) is the one door a loader calls with a file's bytes: a first byte of zero is the tree encoding, anything else is UTF-8 text (a byte order mark skipped), so the loader never knows which it got. An empty span is one error.
- **`ToTree(value, type, registry, resolvers, prior)` and `FromTree(tree, type, registry, resolvers, value, path, logger)`** (`Bridge.hpp`) walk a described value through its description's visitor into a tree and back. The read is tolerant: what reads is kept, what does not takes its default, and `BridgeResult` counts what was defaulted, dropped and left unresolved. `Resolvers` turn `reference` and `entity` fields into the strings a file holds; `prior`, the tree a value was read from, keeps the file's comments through a rewrite. A tree-backed type (a C# data asset type's) is a tree already and has no value in memory, so both refuse it with one error: `ToTree` gives `null` and `FromTree` `TreeBacked`.
- **`RegisterUpgrader(registry, typeName, fromVersion, upgrader, logger)` and `Upgrade(registry, typeName, fromVersion, tree, path, logger)`** (`Upgrade.hpp`) step a tree written at an older version of its type up to the type's `@version`, one version at a time, before `FromTree` reads it. `UnregisterUpgraders(registry, typeName)` removes a type's upgraders, so a script reload can register them again.

Every reader returns its located diagnostics, `path:line:column: message`, each already logged once. The one case that logs nothing is a type the registry lacks: `Upgrade` and `FromTree` both report `UnknownType`, and the caller says what it means.

## A game's save file

There is no envelope helper: `format`, `type`, `version`, `data` and `sub` are `FenrirAssets`' keys, and a game's file picks its own. A save file here holds the player's progress under `progress` and the version its type was at under `progressVersion`. `MyGame::Progress` is a described type at `@version 2`, so `Fenrir::Describe<Progress>` names its description; at version 1 its `health` was `hp`.

`ReadSave` takes the save's bytes as the game read them, text or encoded alike, so a shipped build can cook its saves into the tree encoding (`EncodeTree` of the tree `ReadText` gives) and nothing here changes. The test `Tests/ReadmeExample.cpp` compiles this block from this file and runs it.

```cpp
#include "FenrirSerialisation/Bridge.hpp"
#include "FenrirSerialisation/ReadTree.hpp"
#include "FenrirSerialisation/Text.hpp"
#include "FenrirSerialisation/Upgrade.hpp"
#include "FenrirSerialisation/Value.hpp"

#include "FenrirLogger/ILogger.hpp"
#include "FenrirTypes/Describe.hpp"
#include "FenrirTypes/TypeRegistry.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <string_view>

namespace MyGame
{
    namespace Ser = Fenrir::Serialisation;

    // Registers the upgraders `Progress` needs, once at start-up: version 1 called `health` `hp`.
    bool RegisterProgressUpgraders(Fenrir::TypeRegistry& registry, Fenrir::ILogger& logger)
    {
        return Ser::RegisterUpgrader(
            registry, "MyGame::Progress", 1,
            [](std::uint32_t, Ser::Value& tree) {
                if (tree.Kind() != Ser::ValueKind::Object)
                    return;
                for (Ser::Member& member : tree.AsObject().members)
                    if (member.key == "hp")
                        member.key = "health";
            },
            logger);
    }

    // The save file's text: the progress under `progress`, and its type's version under `progressVersion`.
    std::string WriteSave(const Progress& progress, const Fenrir::TypeRegistry& registry)
    {
        const Fenrir::TypeDescription& type = Fenrir::Describe<Progress>::Description();
        Ser::Object save;
        save.Add("progressVersion", static_cast<std::int64_t>(type.version));
        save.Add("progress", Ser::ToTree(&progress, type, registry, Ser::Resolvers{}));
        return Ser::WriteText(save);
    }

    // The version a save names, or 0 when it names none. Text reads a whole number as a signed integer, and a tree
    // built in code may hold it unsigned, so both are taken.
    std::uint32_t SavedVersion(const Ser::Value* version)
    {
        if (version == nullptr || version->Kind() != Ser::ValueKind::Number)
            return 0;
        const Ser::Number& number = version->AsNumber();
        constexpr std::uint32_t Max = std::numeric_limits<std::uint32_t>::max();
        if (number.Width() == Ser::NumberWidth::Signed && number.AsSigned() >= 1 && number.AsSigned() <= Max)
            return static_cast<std::uint32_t>(number.AsSigned());
        if (number.Width() == Ser::NumberWidth::Unsigned && number.AsUnsigned() >= 1 && number.AsUnsigned() <= Max)
            return static_cast<std::uint32_t>(number.AsUnsigned());
        return 0;
    }

    // Reads a save file's bytes, text or encoded, into `progress`, which holds `Progress`'s defaults on entry. False
    // when there is no progress to read; a field that does not read keeps its default. Every diagnostic is logged.
    bool ReadSave(std::span<const std::byte> bytes, std::string_view path, const Fenrir::TypeRegistry& registry,
                  Progress& progress, Fenrir::ILogger& logger)
    {
        Ser::ReadTreeResult read = Ser::ReadTree(bytes, path, logger);
        if (!read.tree)
            return false;
        if (read.tree->Kind() != Ser::ValueKind::Object)
        {
            logger.Error("{}: expected a save file's object, found another value", path);
            return false;
        }
        Ser::Object& save = read.tree->AsObject();
        const std::uint32_t version = SavedVersion(save.Find("progressVersion"));
        Ser::Value* tree = save.Find("progress");
        if (version == 0 || tree == nullptr)
        {
            logger.Error("{}: expected `progressVersion` and `progress`, found no progress", path);
            return false;
        }

        const Fenrir::TypeDescription& type = Fenrir::Describe<Progress>::Description();
        const Ser::UpgradeResult upgraded = Ser::Upgrade(registry, type.name, version, *tree, path, logger);
        if (upgraded.status == Ser::UpgradeStatus::UnknownType)
        {
            // Upgrade and FromTree log nothing for a type the registry lacks, so the game says so itself.
            logger.Error("{}: expected `{}` in the type registry, found none", path, type.name);
            return false;
        }
        if (upgraded.status == Ser::UpgradeStatus::MissingStep)
            return false;
        const Ser::BridgeResult result =
            Ser::FromTree(*tree, type, registry, Ser::Resolvers{}, &progress, path, logger);
        return result.status != Ser::BridgeStatus::UnknownType;
    }
} // namespace MyGame
```

A save the player edited keeps the comments in its progress when the game writes it again by passing the `progress` tree it read as `ToTree`'s `prior`; comments on the save's own keys are the game's to carry over, as its keys are.
