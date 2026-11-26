#pragma once

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "AssetsTest.hpp"
#include "FenrirSerialisation/Diagnostic.hpp"
#include "FenrirTypes/TypeRegistry.hpp"
#include "Generated/DataAssetsTestPackageTypes.hpp"

namespace AssetsTest
{
    // An App with assets installed and the test package's data asset types registered, as a game registers its own.
    struct DataApp : AssetsApp
    {
        bool registered =
            Fenrir::Generated::RegisterDataAssetsTestPackageTypes(app.GetResource<Fenrir::TypeRegistry>());

        DataApp() = default;
        explicit DataApp(Fenrir::AssetsConfig config) : AssetsApp(std::move(config))
        {
        }
    };

    // How many of `messages` contain `part`.
    inline std::size_t CountContaining(const std::vector<std::string>& messages, std::string_view part)
    {
        return static_cast<std::size_t>(std::ranges::count_if(
            messages, [&](const std::string& message) { return message.find(part) != std::string::npos; }));
    }

    // The diagnostic whose message contains `part`, or null.
    inline const Fenrir::Serialisation::Diagnostic* FindDiagnostic(
        const std::vector<Fenrir::Serialisation::Diagnostic>& all, std::string_view part)
    {
        const auto found = std::ranges::find_if(all, [&](const Fenrir::Serialisation::Diagnostic& diagnostic) {
            return diagnostic.message.find(part) != std::string::npos;
        });
        return found != all.end() ? &*found : nullptr;
    }
} // namespace AssetsTest
