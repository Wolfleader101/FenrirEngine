#pragma once

#include "FenrirSerialisation/Diagnostic.hpp"
#include "FenrirSerialisation/Text.hpp"
#include "FenrirSerialisation/Value.hpp"

#include "FenrirLogger/ILogger.hpp"

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <span>
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

    // The tree `text` reads as, its diagnostics naming `path`; the text must be valid JSON5.
    inline Fenrir::Serialisation::Value TreeOf(std::string_view text, std::string_view path = "test.json5")
    {
        RecordingLogger logger;
        return Fenrir::Serialisation::ReadText(text, path, logger).tree.value_or(Fenrir::Serialisation::Value{});
    }

    // `text` with its first `from` replaced by `to`; unchanged when `from` is not in it.
    inline std::string Replaced(std::string_view text, std::string_view from, std::string_view to)
    {
        std::string result(text);
        const std::size_t at = result.find(from);
        if (at != std::string::npos)
            result.replace(at, from.size(), to);
        return result;
    }

    // A fixture's bytes, by its path under the fixtures directory.
    inline std::string ReadFixture(std::string_view name)
    {
        return ReadBytes(Fixtures() / name);
    }

    // The bytes of `text`, as a file holding it stores them.
    inline std::span<const std::byte> BytesOf(std::string_view text)
    {
        return std::as_bytes(std::span(text.data(), text.size()));
    }

    // The first error `logger` holds, or "no error", for an INFO.
    inline std::string FirstError(const RecordingLogger& logger)
    {
        return logger.errors.empty() ? std::string("no error") : logger.errors.front();
    }
} // namespace SerialisationTest
