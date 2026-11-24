#include "FenrirSerialisation/ReadTree.hpp"

#include "FenrirSerialisation/Encoding.hpp"
#include "FenrirSerialisation/Text.hpp"

#include "FenrirLogger/ILogger.hpp"

#include <string>
#include <string_view>
#include <utility>

namespace Fenrir::Serialisation
{
    ReadTreeResult ReadTree(std::span<const std::byte> bytes, std::string_view path, ILogger& logger)
    {
        if (bytes.empty())
        {
            ReadTreeResult result;
            Diagnostic diagnostic;
            diagnostic.severity = DiagnosticSeverity::Error;
            diagnostic.path = std::string(path);
            diagnostic.message = "expected JSON5 text or the tree encoding, found no bytes";
            logger.Error("{}", FormatDiagnostic(diagnostic));
            result.diagnostics.push_back(std::move(diagnostic));
            return result;
        }
        // The tree encoding's tag starts with a zero, which no JSON5 text can: one byte tells them apart.
        if (bytes.front() == std::byte{0})
            return DecodeTree(bytes, path, logger);
        // ReadText skips a byte order mark itself, so the mark takes no column.
        return ReadText(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()), path, logger);
    }
} // namespace Fenrir::Serialisation
