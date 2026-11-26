#include "Envelope.hpp"

#include <cstdint>
#include <format>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "AssetStoreState.hpp"
#include "FenrirLogger/ILogger.hpp"
#include "FenrirSerialisation/ReadTree.hpp"
#include "FenrirSerialisation/Upgrade.hpp"
#include "FenrirSerialisation/Value.hpp"
#include "FenrirTypes/TypeDescription.hpp"
#include "FenrirTypes/TypeRegistry.hpp"

namespace Fenrir::Detail
{
    namespace
    {
        namespace Ser = Serialisation;

        // The envelope's file format version (ADR-0077 §2): the one this reader reads.
        constexpr std::uint64_t EnvelopeFormat = 1;

        // How a diagnostic names what it found: the node's kind, with a string's or number's text.
        std::string Found(const Ser::Value* node)
        {
            if (node == nullptr)
            {
                return "none";
            }
            switch (node->Kind())
            {
            case Ser::ValueKind::Object:
                return "an object";
            case Ser::ValueKind::Array:
                return "an array";
            case Ser::ValueKind::String:
                return std::format("\"{}\"", node->AsString());
            case Ser::ValueKind::Number: {
                const Ser::Number& number = node->AsNumber();
                switch (number.Width())
                {
                case Ser::NumberWidth::Signed:
                    return std::to_string(number.AsSigned());
                case Ser::NumberWidth::Unsigned:
                    return std::to_string(number.AsUnsigned());
                default:
                    return "a fractional number";
                }
            }
            case Ser::ValueKind::Bool:
                return node->AsBool() ? "true" : "false";
            case Ser::ValueKind::Null:
                return "null";
            }
            return "a value";
        }

        // A whole number from 1 up to `max`, as text reads it signed and code may build it unsigned; none for any other
        // node.
        std::optional<std::uint64_t> PositiveWhole(const Ser::Value* node, std::uint64_t max)
        {
            if (node == nullptr || node->Kind() != Ser::ValueKind::Number)
            {
                return std::nullopt;
            }
            const Ser::Number& number = node->AsNumber();
            if (number.Width() == Ser::NumberWidth::Signed && number.AsSigned() >= 1 &&
                static_cast<std::uint64_t>(number.AsSigned()) <= max)
            {
                return static_cast<std::uint64_t>(number.AsSigned());
            }
            if (number.Width() == Ser::NumberWidth::Unsigned && number.AsUnsigned() >= 1 && number.AsUnsigned() <= max)
            {
                return number.AsUnsigned();
            }
            return std::nullopt;
        }

        // A located diagnostic of the reader's own, at `node`, or at `fallback` when the node is missing or built in
        // code.
        Ser::Diagnostic LocatedAt(Ser::DiagnosticSeverity severity, std::string_view path, const Ser::Value* node,
                                  const Ser::Value& fallback, std::string message)
        {
            const Ser::Value& at = node != nullptr && node->location ? *node : fallback;
            Ser::Diagnostic diagnostic{.severity = severity, .path = std::string(path), .message = std::move(message)};
            if (at.location)
            {
                diagnostic.line = at.location->line;
                diagnostic.column = at.location->column;
            }
            return diagnostic;
        }

        void Append(std::vector<Ser::Diagnostic>& to, std::vector<Ser::Diagnostic>&& from)
        {
            to.insert(to.end(), std::make_move_iterator(from.begin()), std::make_move_iterator(from.end()));
        }
    } // namespace

    EnvelopeRead ReadDataEnvelope(std::span<const std::byte> bytes, std::string_view path,
                                  const TypeDescription& expected, const TypeRegistry& registry,
                                  const Serialisation::Resolvers& resolvers, ILogger& logger)
    {
        EnvelopeRead read;
        // The read fails with `reason`; `diagnostic`, the reader's own, is the store's to log with the failure.
        const auto fail = [&](LoadFailure reason, Ser::Diagnostic diagnostic) -> EnvelopeRead {
            read.failure = reason;
            read.failureMessage = Ser::FormatDiagnostic(diagnostic);
            read.diagnostics.push_back(std::move(diagnostic));
            return std::move(read);
        };

        Ser::ReadTreeResult tree = Ser::ReadTree(bytes, path, logger);
        Append(read.diagnostics, std::move(tree.diagnostics));
        if (!tree.tree)
        {
            read.failure = LoadFailure::ImportFailed;
            read.failureMessage = "the file is neither JSON5 nor the tree encoding";
            return read;
        }
        const Ser::Value& root = *tree.tree;
        if (root.Kind() != Ser::ValueKind::Object)
        {
            return fail(LoadFailure::ImportFailed,
                        LocatedAt(Ser::DiagnosticSeverity::Error, path, &root, root,
                                  std::format("expected a data asset's envelope object, found {}", Found(&root))));
        }
        Ser::Object& envelope = tree.tree->AsObject();

        const Ser::Value* format = envelope.Find("format");
        if (PositiveWhole(format, EnvelopeFormat) != EnvelopeFormat)
        {
            return fail(LoadFailure::ImportFailed,
                        LocatedAt(Ser::DiagnosticSeverity::Error, path, format, root,
                                  std::format("expected `format` {}, found {}", EnvelopeFormat, Found(format))));
        }

        const Ser::Value* typeName = envelope.Find("type");
        if (typeName == nullptr || typeName->Kind() != Ser::ValueKind::String)
        {
            return fail(LoadFailure::ImportFailed,
                        LocatedAt(Ser::DiagnosticSeverity::Error, path, typeName, root,
                                  std::format("expected `type`, a type's name, found {}", Found(typeName))));
        }
        const TypeDescription* type = registry.Find(typeName->AsString());
        if (type == nullptr)
        {
            return fail(
                LoadFailure::WrongType,
                LocatedAt(Ser::DiagnosticSeverity::Error, path, typeName, root,
                          std::format("expected `{}` or a type extending it, found `{}`, which no package registers",
                                      expected.name, typeName->AsString())));
        }
        if (!registry.IsA(type->name, expected.name))
        {
            return fail(
                LoadFailure::WrongType,
                LocatedAt(Ser::DiagnosticSeverity::Error, path, typeName, root,
                          std::format("expected `{}` or a type extending it, found `{}`", expected.name, type->name)));
        }

        const Ser::Value* version = envelope.Find("version");
        const std::optional<std::uint64_t> fromVersion =
            PositiveWhole(version, std::numeric_limits<std::uint32_t>::max());
        if (!fromVersion)
        {
            return fail(LoadFailure::ImportFailed,
                        LocatedAt(Ser::DiagnosticSeverity::Error, path, version, root,
                                  std::format("expected `version`, a whole number from 1, found {}", Found(version))));
        }

        Ser::Value* data = envelope.Find("data");
        if (data == nullptr)
        {
            return fail(LoadFailure::ImportFailed, LocatedAt(Ser::DiagnosticSeverity::Error, path, data, root,
                                                             "expected `data`, the asset's fields, found none"));
        }

        // Embedded sub-assets are a later ticket's: the asset's own data still reads.
        if (const Ser::Value* sub = envelope.Find("sub"); sub != nullptr)
        {
            Ser::Diagnostic warning = LocatedAt(Ser::DiagnosticSeverity::Warning, path, sub, root,
                                                "expected no `sub`, found sub-assets, which are not read yet: skipped");
            logger.Warn("{}", Ser::FormatDiagnostic(warning));
            read.diagnostics.push_back(std::move(warning));
        }

        Ser::UpgradeResult upgraded =
            Ser::Upgrade(registry, type->name, static_cast<std::uint32_t>(*fromVersion), *data, path, logger);
        Append(read.diagnostics, std::move(upgraded.diagnostics));

        auto instance = std::make_unique<DataValue>(*type);
        Ser::BridgeResult bridged = Ser::FromTree(*data, *type, registry, resolvers, instance->value, path, logger);
        Append(read.diagnostics, std::move(bridged.diagnostics));
        read.incomplete = bridged.status == Ser::BridgeStatus::Partial;
        read.type = type;
        read.value = AssetBox(instance.release(), &DestroyAssetData<DataValue>);
        return read;
    }
} // namespace Fenrir::Detail
