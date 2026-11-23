#pragma once

#include "FenrirLogger/ILogger.hpp"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace SerialisationTest
{
    // Keeps what was logged as errors and as warnings, so a test can check what a reader reported and through which
    // door, as AssetsTest::RecordingLogger does for FenrirAssets.
    class RecordingLogger : public Fenrir::ILogger
    {
      public:
        std::vector<std::string> errors;
        std::vector<std::string> warnings;

        void AddLogger(std::shared_ptr<ILogger>) override
        {
        }
        void RemoveLogger(std::shared_ptr<ILogger>) override
        {
        }

      protected:
        void LogImpl(std::string_view) override
        {
        }
        void InfoImpl(std::string_view) override
        {
        }
        void WarnImpl(std::string_view message) override
        {
            warnings.emplace_back(message);
        }
        void ErrorImpl(std::string_view message) override
        {
            errors.emplace_back(message);
        }
        void FatalImpl(std::string_view message) override
        {
            errors.emplace_back(message);
        }
    };

    // The fixtures directory, which FENRIR_SERIALISATION_FIXTURES names.
    inline std::filesystem::path Fixtures()
    {
        return std::filesystem::path(FENRIR_SERIALISATION_FIXTURES);
    }

    // A file's bytes, unchanged: binary mode, so CRLF and a BOM arrive as they are stored.
    inline std::string ReadBytes(const std::filesystem::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    }

    // A fixture's bytes, by its path under the fixtures directory.
    inline std::string ReadFixture(std::string_view name)
    {
        return ReadBytes(Fixtures() / name);
    }
} // namespace SerialisationTest
