#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <system_error>
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

    // The bytes of `file`.
    inline std::string ReadFile(const std::filesystem::path& file)
    {
        std::ifstream stream(file, std::ios::binary);
        return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    }

    // An asset root of the test's own in the system's temporary directory, removed with everything in it when the
    // test ends, so a save or a move never writes into the fixtures.
    class TempRoot
    {
      public:
        TempRoot()
        {
            static std::atomic<int> made = 0;
            m_root = std::filesystem::temp_directory_path() /
                     std::format("FenrirAssetsTest-{}-{}", std::chrono::steady_clock::now().time_since_epoch().count(),
                                 made++);
            std::filesystem::create_directories(m_root);
        }
        TempRoot(const TempRoot&) = delete;
        TempRoot& operator=(const TempRoot&) = delete;
        ~TempRoot()
        {
            std::error_code error;
            std::filesystem::remove_all(m_root, error);
        }

        [[nodiscard]] const std::filesystem::path& Path() const noexcept
        {
            return m_root;
        }

        // Copies `from` to the asset path `to` under the root.
        void Copy(const std::filesystem::path& from, std::string_view to) const
        {
            const std::filesystem::path target = m_root / to;
            std::filesystem::create_directories(target.parent_path());
            std::filesystem::copy_file(from, target, std::filesystem::copy_options::overwrite_existing);
        }

        // Writes `text` to the asset path `to` under the root.
        void Write(std::string_view to, std::string_view text) const
        {
            const std::filesystem::path target = m_root / to;
            std::filesystem::create_directories(target.parent_path());
            std::ofstream stream(target, std::ios::binary | std::ios::trunc);
            stream.write(text.data(), static_cast<std::streamsize>(text.size()));
        }

      private:
        std::filesystem::path m_root;
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
