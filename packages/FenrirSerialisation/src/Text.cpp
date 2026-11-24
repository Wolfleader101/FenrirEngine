#include "FenrirSerialisation/Text.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <string_view>
#include <system_error>

namespace Fenrir::Serialisation
{
    namespace
    {
        constexpr std::string_view Indent = "  ";

        bool IsScalar(const Value& value)
        {
            return value.Kind() != ValueKind::Object && value.Kind() != ValueKind::Array;
        }

        bool IsIdentifierStart(char c)
        {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '$';
        }

        bool IsIdentifierPart(char c)
        {
            return IsIdentifierStart(c) || (c >= '0' && c <= '9');
        }

        // An ECMAScript IdentifierName in ASCII. A key with other UTF-8 in it is quoted, which every JSON5 reader
        // reads, so the writer needs no Unicode ID_Start and ID_Continue tables.
        bool IsBareKey(std::string_view key)
        {
            return !key.empty() && IsIdentifierStart(key.front()) &&
                   std::all_of(key.begin() + 1, key.end(), IsIdentifierPart);
        }

        void WriteString(std::string& out, std::string_view text)
        {
            out += '"';
            for (const char c : text)
            {
                switch (c)
                {
                case '"':
                    out += "\\\"";
                    break;
                case '\\':
                    out += "\\\\";
                    break;
                case '\n':
                    out += "\\n";
                    break;
                case '\t':
                    out += "\\t";
                    break;
                case '\r':
                    out += "\\r";
                    break;
                case '\b':
                    out += "\\b";
                    break;
                case '\f':
                    out += "\\f";
                    break;
                default:
                    if (static_cast<unsigned char>(c) < 0x20)
                    {
                        constexpr std::string_view digits = "0123456789abcdef";
                        out += "\\u00";
                        out += digits[static_cast<unsigned char>(c) >> 4];
                        out += digits[static_cast<unsigned char>(c) & 0xf];
                    }
                    else
                    {
                        out += c;
                    }
                }
            }
            out += '"';
        }

        void WriteKey(std::string& out, std::string_view key)
        {
            if (IsBareKey(key))
                out += key;
            else
                WriteString(out, key);
        }

        template <typename T>
        void WriteShortest(std::string& out, T value)
        {
            if constexpr (std::is_floating_point_v<T>)
            {
                if (std::isnan(value))
                {
                    out += "NaN";
                    return;
                }
                if (std::isinf(value))
                {
                    out += value < 0 ? "-Infinity" : "Infinity";
                    return;
                }
            }
            // Wide enough for the longest shortest-form double, `-2.2250738585072014e-308`, and any 64-bit integer.
            std::array<char, 32> buffer{};
            const std::to_chars_result result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
            out.append(buffer.data(), result.ptr);
        }

        void WriteNumber(std::string& out, const Number& number)
        {
            switch (number.Width())
            {
            case NumberWidth::Signed:
                WriteShortest(out, number.AsSigned());
                break;
            case NumberWidth::Unsigned:
                WriteShortest(out, number.AsUnsigned());
                break;
            case NumberWidth::Single:
                WriteShortest(out, number.AsSingle());
                break;
            case NumberWidth::Double:
                WriteShortest(out, number.AsDouble());
                break;
            }
        }

        void WriteScalar(std::string& out, const Value& value)
        {
            switch (value.Kind())
            {
            case ValueKind::String:
                WriteString(out, value.AsString());
                break;
            case ValueKind::Number:
                WriteNumber(out, value.AsNumber());
                break;
            case ValueKind::Bool:
                out += value.AsBool() ? "true" : "false";
                break;
            default:
                out += "null";
                break;
            }
        }

        template <typename Item>
        bool HasComments(const std::vector<Item>& items, const std::vector<std::string>& closingComments)
        {
            return !closingComments.empty() || std::any_of(items.begin(), items.end(), [](const Item& item) {
                return !item.leadingComments.empty() || item.trailingComment.has_value();
            });
        }

        // The flow rule (the spec, "The JSON5 writer"): an array of scalars, and an all-scalar object that is an array
        // element or has one member. A container holding a comment is block style, which has a line to put it on.
        bool IsFlow(const Value& value, bool isArrayElement)
        {
            if (value.Kind() == ValueKind::Array)
            {
                const Array& array = value.AsArray();
                return !HasComments(array.elements, array.closingComments) &&
                       std::all_of(array.elements.begin(), array.elements.end(),
                                   [](const Element& element) { return IsScalar(element.value); });
            }
            const Object& object = value.AsObject();
            if (HasComments(object.members, object.closingComments))
                return false;
            const bool allScalar = std::all_of(object.members.begin(), object.members.end(),
                                               [](const Member& member) { return IsScalar(member.value); });
            return allScalar && (object.members.empty() || isArrayElement || object.members.size() == 1);
        }

        void WriteIndent(std::string& out, int depth)
        {
            for (int i = 0; i < depth; ++i)
                out += Indent;
        }

        void WriteCommentLines(std::string& out, const std::vector<std::string>& comments, int depth)
        {
            for (const std::string& comment : comments)
            {
                WriteIndent(out, depth);
                out += comment;
                out += '\n';
            }
        }

        void WriteFlow(std::string& out, const Value& value)
        {
            if (value.Kind() == ValueKind::Array)
            {
                out += '[';
                const char* separator = "";
                for (const Element& element : value.AsArray().elements)
                {
                    out += separator;
                    WriteScalar(out, element.value);
                    separator = ", ";
                }
                out += ']';
                return;
            }
            out += '{';
            const char* separator = "";
            for (const Member& member : value.AsObject().members)
            {
                out += separator;
                WriteKey(out, member.key);
                out += ": ";
                WriteScalar(out, member.value);
                separator = ", ";
            }
            out += '}';
        }

        void WriteValue(std::string& out, const Value& value, int depth, bool isArrayElement);

        void WriteItemEnd(std::string& out, const std::optional<std::string>& trailingComment)
        {
            out += ',';
            if (trailingComment)
            {
                out += ' ';
                out += *trailingComment;
            }
            out += '\n';
        }

        void WriteBlock(std::string& out, const Value& value, int depth)
        {
            if (value.Kind() == ValueKind::Array)
            {
                const Array& array = value.AsArray();
                out += "[\n";
                for (const Element& element : array.elements)
                {
                    WriteCommentLines(out, element.leadingComments, depth + 1);
                    WriteIndent(out, depth + 1);
                    WriteValue(out, element.value, depth + 1, true);
                    WriteItemEnd(out, element.trailingComment);
                }
                WriteCommentLines(out, array.closingComments, depth + 1);
                WriteIndent(out, depth);
                out += ']';
                return;
            }
            const Object& object = value.AsObject();
            out += "{\n";
            for (const Member& member : object.members)
            {
                WriteCommentLines(out, member.leadingComments, depth + 1);
                WriteIndent(out, depth + 1);
                WriteKey(out, member.key);
                out += ": ";
                WriteValue(out, member.value, depth + 1, false);
                WriteItemEnd(out, member.trailingComment);
            }
            WriteCommentLines(out, object.closingComments, depth + 1);
            WriteIndent(out, depth);
            out += '}';
        }

        void WriteValue(std::string& out, const Value& value, int depth, bool isArrayElement)
        {
            if (IsScalar(value))
                WriteScalar(out, value);
            else if (IsFlow(value, isArrayElement))
                WriteFlow(out, value);
            else
                WriteBlock(out, value, depth);
        }
    } // namespace

    std::string WriteText(const Value& root)
    {
        std::string out;
        WriteCommentLines(out, root.leadingComments, 0);
        WriteValue(out, root, 0, false);
        out += '\n';
        return out;
    }
} // namespace Fenrir::Serialisation
