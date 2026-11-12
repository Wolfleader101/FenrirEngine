#include "StyleValues.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace Fenrir::Detail
{
    bool IsSpace(char c)
    {
        return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
    }

    std::string_view Trim(std::string_view text)
    {
        while (!text.empty() && IsSpace(text.front()))
        {
            text.remove_prefix(1);
        }
        while (!text.empty() && IsSpace(text.back()))
        {
            text.remove_suffix(1);
        }
        return text;
    }

    char Lower(char c)
    {
        return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
    }

    std::string Lowered(std::string_view text)
    {
        std::string out(text);
        std::transform(out.begin(), out.end(), out.begin(), Lower);
        return out;
    }

    namespace
    {
        bool EqualsIgnoringCase(std::string_view a, std::string_view b)
        {
            return a.size() == b.size() &&
                   std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) { return Lower(x) == Lower(y); });
        }

        bool Fail(StyleParseContext& context, std::string message)
        {
            context.error = std::move(message);
            return false;
        }

        /**
         * @brief `text` cut at each top-level `separator` (a comma, or any run of spaces when it is a space), keeping
         * what is inside parentheses whole.
         */
        std::vector<std::string_view> Split(std::string_view text, char separator)
        {
            std::vector<std::string_view> parts;
            int depth = 0;
            std::size_t start = 0;
            for (std::size_t i = 0; i <= text.size(); ++i)
            {
                const bool atEnd = i == text.size();
                const char c = atEnd ? '\0' : text[i];
                if (c == '(')
                {
                    ++depth;
                }
                else if (c == ')')
                {
                    --depth;
                }
                const bool cut = atEnd || (depth == 0 && (separator == ' ' ? IsSpace(c) : c == separator));
                if (cut)
                {
                    const std::string_view part = Trim(text.substr(start, i - start));
                    if (!part.empty() || separator != ' ')
                    {
                        parts.push_back(part);
                    }
                    start = i + 1;
                }
            }
            return parts;
        }

        /** @brief `name(arguments)`: the arguments when `text` is a call of `name`. */
        std::optional<std::string_view> CallOf(std::string_view text, std::string_view name)
        {
            text = Trim(text);
            if (text.size() < name.size() + 2 || !EqualsIgnoringCase(text.substr(0, name.size()), name) ||
                text[name.size()] != '(' || text.back() != ')')
            {
                return std::nullopt;
            }
            return Trim(text.substr(name.size() + 1, text.size() - name.size() - 2));
        }

        /** @brief A number and the unit after it (empty for none), or nothing when `text` does not start with one. */
        std::optional<std::pair<float, std::string_view>> NumberAndUnit(std::string_view text)
        {
            text = Trim(text);
            if (text.empty())
            {
                return std::nullopt;
            }
            // from_chars takes no leading plus.
            if (text.front() == '+')
            {
                text.remove_prefix(1);
            }
            float value = 0.0f;
            const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
            if (error != std::errc{} || !std::isfinite(value))
            {
                return std::nullopt;
            }
            return std::pair{value, text.substr(static_cast<std::size_t>(end - text.data()))};
        }

        std::optional<float> PlainNumber(std::string_view text)
        {
            const auto number = NumberAndUnit(text);
            if (!number || !number->second.empty())
            {
                return std::nullopt;
            }
            return number->first;
        }

        /** @brief A length in `px`, or `0` with no unit. */
        std::optional<float> Pixels(std::string_view text)
        {
            const auto number = NumberAndUnit(text);
            if (!number)
            {
                return std::nullopt;
            }
            if (EqualsIgnoringCase(number->second, "px") || (number->second.empty() && number->first == 0.0f))
            {
                return number->first;
            }
            return std::nullopt;
        }

        std::optional<Length> ReadLength(std::string_view text)
        {
            text = Trim(text);
            if (EqualsIgnoringCase(text, "auto") || EqualsIgnoringCase(text, "none"))
            {
                return Length::Auto();
            }
            const auto number = NumberAndUnit(text);
            if (!number)
            {
                return std::nullopt;
            }
            if (number->second == "%")
            {
                return Length::Percent(number->first);
            }
            if (const auto pixels = Pixels(text))
            {
                return Length::Px(*pixels);
            }
            return std::nullopt;
        }

        /** @brief An angle in `deg`, `rad`, `grad` or `turn` (or `0`), in radians. */
        std::optional<float> Angle(std::string_view text)
        {
            const auto number = NumberAndUnit(text);
            if (!number)
            {
                return std::nullopt;
            }
            const auto [value, unit] = *number;
            constexpr float pi = std::numbers::pi_v<float>;
            if (EqualsIgnoringCase(unit, "deg"))
            {
                return value * pi / 180.0f;
            }
            if (EqualsIgnoringCase(unit, "rad"))
            {
                return value;
            }
            if (EqualsIgnoringCase(unit, "grad"))
            {
                return value * pi / 200.0f;
            }
            if (EqualsIgnoringCase(unit, "turn"))
            {
                return value * 2.0f * pi;
            }
            if (unit.empty() && value == 0.0f)
            {
                return 0.0f;
            }
            return std::nullopt;
        }

        // Colour's own transfer function is private, and rgb() and hsl() give channels that are not whole bytes.
        float SrgbToLinear(float encoded)
        {
            return encoded <= 0.04045f ? encoded / 12.92f : std::pow((encoded + 0.055f) / 1.055f, 2.4f);
        }

        /** @brief A colour from sRGB channels 0 to 1 and an alpha. */
        Colour FromSrgb(float r, float g, float b, float a)
        {
            const auto channel = [](float v) { return SrgbToLinear(std::clamp(v, 0.0f, 1.0f)); };
            return {channel(r), channel(g), channel(b), std::clamp(a, 0.0f, 1.0f)};
        }

        struct NamedColour
        {
            std::string_view name;
            std::uint32_t rgb;
        };

        // CSS Color Module Level 4's named colours.
        constexpr std::array<NamedColour, 148> NamedColours{{
            {"aliceblue", 0xF0F8FF},
            {"antiquewhite", 0xFAEBD7},
            {"aqua", 0x00FFFF},
            {"aquamarine", 0x7FFFD4},
            {"azure", 0xF0FFFF},
            {"beige", 0xF5F5DC},
            {"bisque", 0xFFE4C4},
            {"black", 0x000000},
            {"blanchedalmond", 0xFFEBCD},
            {"blue", 0x0000FF},
            {"blueviolet", 0x8A2BE2},
            {"brown", 0xA52A2A},
            {"burlywood", 0xDEB887},
            {"cadetblue", 0x5F9EA0},
            {"chartreuse", 0x7FFF00},
            {"chocolate", 0xD2691E},
            {"coral", 0xFF7F50},
            {"cornflowerblue", 0x6495ED},
            {"cornsilk", 0xFFF8DC},
            {"crimson", 0xDC143C},
            {"cyan", 0x00FFFF},
            {"darkblue", 0x00008B},
            {"darkcyan", 0x008B8B},
            {"darkgoldenrod", 0xB8860B},
            {"darkgray", 0xA9A9A9},
            {"darkgreen", 0x006400},
            {"darkgrey", 0xA9A9A9},
            {"darkkhaki", 0xBDB76B},
            {"darkmagenta", 0x8B008B},
            {"darkolivegreen", 0x556B2F},
            {"darkorange", 0xFF8C00},
            {"darkorchid", 0x9932CC},
            {"darkred", 0x8B0000},
            {"darksalmon", 0xE9967A},
            {"darkseagreen", 0x8FBC8F},
            {"darkslateblue", 0x483D8B},
            {"darkslategray", 0x2F4F4F},
            {"darkslategrey", 0x2F4F4F},
            {"darkturquoise", 0x00CED1},
            {"darkviolet", 0x9400D3},
            {"deeppink", 0xFF1493},
            {"deepskyblue", 0x00BFFF},
            {"dimgray", 0x696969},
            {"dimgrey", 0x696969},
            {"dodgerblue", 0x1E90FF},
            {"firebrick", 0xB22222},
            {"floralwhite", 0xFFFAF0},
            {"forestgreen", 0x228B22},
            {"fuchsia", 0xFF00FF},
            {"gainsboro", 0xDCDCDC},
            {"ghostwhite", 0xF8F8FF},
            {"gold", 0xFFD700},
            {"goldenrod", 0xDAA520},
            {"gray", 0x808080},
            {"green", 0x008000},
            {"greenyellow", 0xADFF2F},
            {"grey", 0x808080},
            {"honeydew", 0xF0FFF0},
            {"hotpink", 0xFF69B4},
            {"indianred", 0xCD5C5C},
            {"indigo", 0x4B0082},
            {"ivory", 0xFFFFF0},
            {"khaki", 0xF0E68C},
            {"lavender", 0xE6E6FA},
            {"lavenderblush", 0xFFF0F5},
            {"lawngreen", 0x7CFC00},
            {"lemonchiffon", 0xFFFACD},
            {"lightblue", 0xADD8E6},
            {"lightcoral", 0xF08080},
            {"lightcyan", 0xE0FFFF},
            {"lightgoldenrodyellow", 0xFAFAD2},
            {"lightgray", 0xD3D3D3},
            {"lightgreen", 0x90EE90},
            {"lightgrey", 0xD3D3D3},
            {"lightpink", 0xFFB6C1},
            {"lightsalmon", 0xFFA07A},
            {"lightseagreen", 0x20B2AA},
            {"lightskyblue", 0x87CEFA},
            {"lightslategray", 0x778899},
            {"lightslategrey", 0x778899},
            {"lightsteelblue", 0xB0C4DE},
            {"lightyellow", 0xFFFFE0},
            {"lime", 0x00FF00},
            {"limegreen", 0x32CD32},
            {"linen", 0xFAF0E6},
            {"magenta", 0xFF00FF},
            {"maroon", 0x800000},
            {"mediumaquamarine", 0x66CDAA},
            {"mediumblue", 0x0000CD},
            {"mediumorchid", 0xBA55D3},
            {"mediumpurple", 0x9370DB},
            {"mediumseagreen", 0x3CB371},
            {"mediumslateblue", 0x7B68EE},
            {"mediumspringgreen", 0x00FA9A},
            {"mediumturquoise", 0x48D1CC},
            {"mediumvioletred", 0xC71585},
            {"midnightblue", 0x191970},
            {"mintcream", 0xF5FFFA},
            {"mistyrose", 0xFFE4E1},
            {"moccasin", 0xFFE4B5},
            {"navajowhite", 0xFFDEAD},
            {"navy", 0x000080},
            {"oldlace", 0xFDF5E6},
            {"olive", 0x808000},
            {"olivedrab", 0x6B8E23},
            {"orange", 0xFFA500},
            {"orangered", 0xFF4500},
            {"orchid", 0xDA70D6},
            {"palegoldenrod", 0xEEE8AA},
            {"palegreen", 0x98FB98},
            {"paleturquoise", 0xAFEEEE},
            {"palevioletred", 0xDB7093},
            {"papayawhip", 0xFFEFD5},
            {"peachpuff", 0xFFDAB9},
            {"peru", 0xCD853F},
            {"pink", 0xFFC0CB},
            {"plum", 0xDDA0DD},
            {"powderblue", 0xB0E0E6},
            {"purple", 0x800080},
            {"rebeccapurple", 0x663399},
            {"red", 0xFF0000},
            {"rosybrown", 0xBC8F8F},
            {"royalblue", 0x4169E1},
            {"saddlebrown", 0x8B4513},
            {"salmon", 0xFA8072},
            {"sandybrown", 0xF4A460},
            {"seagreen", 0x2E8B57},
            {"seashell", 0xFFF5EE},
            {"sienna", 0xA0522D},
            {"silver", 0xC0C0C0},
            {"skyblue", 0x87CEEB},
            {"slateblue", 0x6A5ACD},
            {"slategray", 0x708090},
            {"slategrey", 0x708090},
            {"snow", 0xFFFAFA},
            {"springgreen", 0x00FF7F},
            {"steelblue", 0x4682B4},
            {"tan", 0xD2B48C},
            {"teal", 0x008080},
            {"thistle", 0xD8BFD8},
            {"tomato", 0xFF6347},
            {"turquoise", 0x40E0D0},
            {"violet", 0xEE82EE},
            {"wheat", 0xF5DEB3},
            {"white", 0xFFFFFF},
            {"whitesmoke", 0xF5F5F5},
            {"yellow", 0xFFFF00},
            {"yellowgreen", 0x9ACD32},
        }};

        int HexDigit(char c)
        {
            if (c >= '0' && c <= '9')
            {
                return c - '0';
            }
            c = Lower(c);
            if (c >= 'a' && c <= 'f')
            {
                return c - 'a' + 10;
            }
            return -1;
        }

        std::optional<Colour> HexColour(std::string_view digits)
        {
            std::array<int, 8> values{};
            for (std::size_t i = 0; i < digits.size(); ++i)
            {
                values[i] = HexDigit(digits[i]);
                if (values[i] < 0)
                {
                    return std::nullopt;
                }
            }
            std::array<float, 4> channels{0.0f, 0.0f, 0.0f, 1.0f};
            if (digits.size() == 3 || digits.size() == 4)
            {
                for (std::size_t i = 0; i < digits.size(); ++i)
                {
                    channels[i] = static_cast<float>(values[i] * 17) / 255.0f;
                }
            }
            else if (digits.size() == 6 || digits.size() == 8)
            {
                for (std::size_t i = 0; i < digits.size() / 2; ++i)
                {
                    channels[i] = static_cast<float>(values[i * 2] * 16 + values[i * 2 + 1]) / 255.0f;
                }
            }
            else
            {
                return std::nullopt;
            }
            return FromSrgb(channels[0], channels[1], channels[2], channels[3]);
        }

        /** @brief An alpha: a number 0 to 1, or a percentage. */
        std::optional<float> AlphaValue(std::string_view text)
        {
            const auto number = NumberAndUnit(text);
            if (!number)
            {
                return std::nullopt;
            }
            if (number->second == "%")
            {
                return number->first / 100.0f;
            }
            if (number->second.empty())
            {
                return number->first;
            }
            return std::nullopt;
        }

        /**
         * @brief The arguments of `rgb()` and `hsl()`, either comma-separated or space-separated with an alpha after a
         * slash: three components and an optional alpha.
         */
        std::optional<std::vector<std::string_view>> ColourArguments(std::string_view arguments)
        {
            std::vector<std::string_view> parts;
            if (arguments.find(',') != std::string_view::npos)
            {
                parts = Split(arguments, ',');
            }
            else
            {
                const std::size_t slash = arguments.find('/');
                parts = Split(arguments.substr(0, slash), ' ');
                if (slash != std::string_view::npos)
                {
                    parts.push_back(Trim(arguments.substr(slash + 1)));
                }
            }
            if (parts.size() != 3 && parts.size() != 4)
            {
                return std::nullopt;
            }
            return parts;
        }

        float HueToRgb(float p, float q, float t)
        {
            t -= std::floor(t);
            if (t < 1.0f / 6.0f)
            {
                return p + (q - p) * 6.0f * t;
            }
            if (t < 0.5f)
            {
                return q;
            }
            if (t < 2.0f / 3.0f)
            {
                return p + (q - p) * (2.0f / 3.0f - t) * 6.0f;
            }
            return p;
        }

        std::optional<Colour> ReadColour(std::string_view text)
        {
            text = Trim(text);
            if (text.empty())
            {
                return std::nullopt;
            }
            if (text.front() == '#')
            {
                return HexColour(text.substr(1));
            }
            if (EqualsIgnoringCase(text, "transparent"))
            {
                return Colour{0.0f, 0.0f, 0.0f, 0.0f};
            }
            for (const NamedColour& named : NamedColours)
            {
                if (EqualsIgnoringCase(text, named.name))
                {
                    return Colour::FromSrgb((named.rgb << 8u) | 0xFFu);
                }
            }
            std::optional<std::string_view> arguments = CallOf(text, "rgb");
            if (!arguments)
            {
                arguments = CallOf(text, "rgba");
            }
            if (arguments)
            {
                const auto parts = ColourArguments(*arguments);
                if (!parts)
                {
                    return std::nullopt;
                }
                std::array<float, 3> rgb{};
                for (std::size_t i = 0; i < 3; ++i)
                {
                    const auto number = NumberAndUnit((*parts)[i]);
                    if (!number || (!number->second.empty() && number->second != "%"))
                    {
                        return std::nullopt;
                    }
                    rgb[i] = number->second == "%" ? number->first / 100.0f : number->first / 255.0f;
                }
                const std::optional<float> alpha = parts->size() == 4 ? AlphaValue((*parts)[3]) : 1.0f;
                if (!alpha)
                {
                    return std::nullopt;
                }
                return FromSrgb(rgb[0], rgb[1], rgb[2], *alpha);
            }
            arguments = CallOf(text, "hsl");
            if (!arguments)
            {
                arguments = CallOf(text, "hsla");
            }
            if (arguments)
            {
                const auto parts = ColourArguments(*arguments);
                if (!parts)
                {
                    return std::nullopt;
                }
                std::optional<float> hue = PlainNumber((*parts)[0]);
                if (!hue)
                {
                    const std::optional<float> radians = Angle((*parts)[0]);
                    if (!radians)
                    {
                        return std::nullopt;
                    }
                    hue = *radians * 180.0f / std::numbers::pi_v<float>;
                }
                const auto saturation = NumberAndUnit((*parts)[1]);
                const auto lightness = NumberAndUnit((*parts)[2]);
                if (!saturation || saturation->second != "%" || !lightness || lightness->second != "%")
                {
                    return std::nullopt;
                }
                const std::optional<float> alpha = parts->size() == 4 ? AlphaValue((*parts)[3]) : 1.0f;
                if (!alpha)
                {
                    return std::nullopt;
                }
                const float h = *hue / 360.0f;
                const float s = std::clamp(saturation->first / 100.0f, 0.0f, 1.0f);
                const float l = std::clamp(lightness->first / 100.0f, 0.0f, 1.0f);
                const float q = l < 0.5f ? l * (1.0f + s) : l + s - l * s;
                const float p = 2.0f * l - q;
                return FromSrgb(HueToRgb(p, q, h + 1.0f / 3.0f), HueToRgb(p, q, h), HueToRgb(p, q, h - 1.0f / 3.0f),
                                *alpha);
            }
            return std::nullopt;
        }

        /** @brief Gradient stops, each a colour and an optional position; unplaced ones are spread as CSS spreads them.
         */
        std::optional<std::vector<GradientStop>> ReadStops(std::span<const std::string_view> parts)
        {
            std::vector<GradientStop> stops;
            std::vector<bool> placed;
            for (const std::string_view part : parts)
            {
                const std::vector<std::string_view> words = Split(part, ' ');
                if (words.empty() || words.size() > 2)
                {
                    return std::nullopt;
                }
                const std::optional<Colour> colour = ReadColour(words[0]);
                if (!colour)
                {
                    return std::nullopt;
                }
                GradientStop stop{0.0f, *colour};
                if (words.size() == 2)
                {
                    const auto number = NumberAndUnit(words[1]);
                    if (!number || number->second != "%")
                    {
                        return std::nullopt;
                    }
                    stop.offset = number->first / 100.0f;
                }
                stops.push_back(stop);
                placed.push_back(words.size() == 2);
            }
            if (stops.size() < 2)
            {
                return std::nullopt;
            }
            // CSS's fix-up: the ends default to 0 and 1, no stop is before the one ahead of it, and each run of
            // unplaced stops is spread evenly between the placed stops round it.
            if (!placed.front())
            {
                stops.front().offset = 0.0f;
                placed.front() = true;
            }
            if (!placed.back())
            {
                stops.back().offset = 1.0f;
                placed.back() = true;
            }
            float highest = stops.front().offset;
            for (std::size_t i = 0; i < stops.size(); ++i)
            {
                if (placed[i])
                {
                    stops[i].offset = std::max(stops[i].offset, highest);
                    highest = stops[i].offset;
                }
            }
            std::size_t previous = 0;
            for (std::size_t i = 1; i < stops.size(); ++i)
            {
                if (!placed[i])
                {
                    continue;
                }
                for (std::size_t j = previous + 1; j < i; ++j)
                {
                    const float t = static_cast<float>(j - previous) / static_cast<float>(i - previous);
                    stops[j].offset = stops[previous].offset + (stops[i].offset - stops[previous].offset) * t;
                }
                previous = i;
            }
            return stops;
        }

        std::optional<float> SideAngle(std::string_view text)
        {
            const std::vector<std::string_view> words = Split(text, ' ');
            if (words.size() < 2 || !EqualsIgnoringCase(words[0], "to"))
            {
                return std::nullopt;
            }
            float x = 0.0f;
            float y = 0.0f;
            for (std::size_t i = 1; i < words.size(); ++i)
            {
                if (EqualsIgnoringCase(words[i], "left"))
                {
                    x = -1.0f;
                }
                else if (EqualsIgnoringCase(words[i], "right"))
                {
                    x = 1.0f;
                }
                else if (EqualsIgnoringCase(words[i], "top"))
                {
                    y = -1.0f;
                }
                else if (EqualsIgnoringCase(words[i], "bottom"))
                {
                    y = 1.0f;
                }
                else
                {
                    return std::nullopt;
                }
            }
            // A corner is read as the diagonal through it (CSS fits it to the box's aspect; not done here).
            return std::atan2(x, -y) * 180.0f / std::numbers::pi_v<float>;
        }

        std::optional<std::string_view> UrlPath(std::string_view text)
        {
            std::optional<std::string_view> path = CallOf(text, "url");
            if (!path)
            {
                return std::nullopt;
            }
            if (path->size() >= 2 && (path->front() == '"' || path->front() == '\'') && path->back() == path->front())
            {
                *path = path->substr(1, path->size() - 2);
            }
            return path->empty() ? std::nullopt : path;
        }

        template <typename T, std::size_t N>
        bool ParseKeyword(std::string_view value, T& out, StyleParseContext& context,
                          const std::array<std::pair<std::string_view, T>, N>& keywords)
        {
            value = Trim(value);
            for (const auto& [name, keyword] : keywords)
            {
                if (EqualsIgnoringCase(value, name))
                {
                    out = keyword;
                    return true;
                }
            }
            return Fail(context, "it is not one of the property's keywords");
        }

        bool ParseDisplay(std::string_view value, Display& out, StyleParseContext& context)
        {
            return ParseKeyword(value, out, context,
                                std::array{std::pair{std::string_view("flex"), Display::Flex},
                                           std::pair{std::string_view("none"), Display::None}});
        }

        bool ParsePosition(std::string_view value, Position& out, StyleParseContext& context)
        {
            return ParseKeyword(value, out, context,
                                std::array{std::pair{std::string_view("relative"), Position::Relative},
                                           std::pair{std::string_view("absolute"), Position::Absolute}});
        }

        bool ParseFlexDirection(std::string_view value, FlexDirection& out, StyleParseContext& context)
        {
            return ParseKeyword(value, out, context,
                                std::array{std::pair{std::string_view("column"), FlexDirection::Column},
                                           std::pair{std::string_view("column-reverse"), FlexDirection::ColumnReverse},
                                           std::pair{std::string_view("row"), FlexDirection::Row},
                                           std::pair{std::string_view("row-reverse"), FlexDirection::RowReverse}});
        }

        bool ParseFlexWrap(std::string_view value, FlexWrap& out, StyleParseContext& context)
        {
            return ParseKeyword(value, out, context,
                                std::array{std::pair{std::string_view("nowrap"), FlexWrap::NoWrap},
                                           std::pair{std::string_view("wrap"), FlexWrap::Wrap},
                                           std::pair{std::string_view("wrap-reverse"), FlexWrap::WrapReverse}});
        }

        bool ParseAlign(std::string_view value, Align& out, StyleParseContext& context)
        {
            return ParseKeyword(value, out, context,
                                std::array{std::pair{std::string_view("auto"), Align::Auto},
                                           std::pair{std::string_view("flex-start"), Align::FlexStart},
                                           std::pair{std::string_view("start"), Align::FlexStart},
                                           std::pair{std::string_view("center"), Align::Center},
                                           std::pair{std::string_view("flex-end"), Align::FlexEnd},
                                           std::pair{std::string_view("end"), Align::FlexEnd},
                                           std::pair{std::string_view("stretch"), Align::Stretch},
                                           std::pair{std::string_view("baseline"), Align::Baseline},
                                           std::pair{std::string_view("space-between"), Align::SpaceBetween},
                                           std::pair{std::string_view("space-around"), Align::SpaceAround},
                                           std::pair{std::string_view("space-evenly"), Align::SpaceEvenly}});
        }

        bool ParseJustify(std::string_view value, Justify& out, StyleParseContext& context)
        {
            return ParseKeyword(value, out, context,
                                std::array{std::pair{std::string_view("flex-start"), Justify::FlexStart},
                                           std::pair{std::string_view("start"), Justify::FlexStart},
                                           std::pair{std::string_view("center"), Justify::Center},
                                           std::pair{std::string_view("flex-end"), Justify::FlexEnd},
                                           std::pair{std::string_view("end"), Justify::FlexEnd},
                                           std::pair{std::string_view("space-between"), Justify::SpaceBetween},
                                           std::pair{std::string_view("space-around"), Justify::SpaceAround},
                                           std::pair{std::string_view("space-evenly"), Justify::SpaceEvenly}});
        }

        bool ParseOverflow(std::string_view value, Overflow& out, StyleParseContext& context)
        {
            return ParseKeyword(value, out, context,
                                std::array{std::pair{std::string_view("visible"), Overflow::Visible},
                                           std::pair{std::string_view("hidden"), Overflow::Hidden}});
        }

        bool ParsePointerEvents(std::string_view value, PointerEvents& out, StyleParseContext& context)
        {
            return ParseKeyword(value, out, context,
                                std::array{std::pair{std::string_view("none"), PointerEvents::None},
                                           std::pair{std::string_view("all"), PointerEvents::All}});
        }

        bool ParseCursor(std::string_view value, Cursor& out, StyleParseContext& context)
        {
            return ParseKeyword(value, out, context,
                                std::array{std::pair{std::string_view("auto"), Cursor::Auto},
                                           std::pair{std::string_view("default"), Cursor::Default},
                                           std::pair{std::string_view("pointer"), Cursor::Pointer},
                                           std::pair{std::string_view("text"), Cursor::Text},
                                           std::pair{std::string_view("move"), Cursor::Move},
                                           std::pair{std::string_view("not-allowed"), Cursor::NotAllowed},
                                           std::pair{std::string_view("ew-resize"), Cursor::EwResize},
                                           std::pair{std::string_view("ns-resize"), Cursor::NsResize},
                                           std::pair{std::string_view("nwse-resize"), Cursor::NwseResize},
                                           std::pair{std::string_view("nesw-resize"), Cursor::NeswResize},
                                           std::pair{std::string_view("none"), Cursor::None}});
        }

        bool ParseBackgroundSize(std::string_view value, BackgroundSize& out, StyleParseContext& context)
        {
            return ParseKeyword(value, out, context,
                                std::array{std::pair{std::string_view("stretch"), BackgroundSize::Stretch},
                                           std::pair{std::string_view("100% 100%"), BackgroundSize::Stretch},
                                           std::pair{std::string_view("cover"), BackgroundSize::Cover},
                                           std::pair{std::string_view("contain"), BackgroundSize::Contain}});
        }

        bool ParseLength(std::string_view value, Length& out, StyleParseContext& context)
        {
            const std::optional<Length> length = ReadLength(value);
            if (!length)
            {
                return Fail(context, "it is not a length in px or %, or auto");
            }
            out = *length;
            return true;
        }

        bool ParsePixels(std::string_view value, float& out, StyleParseContext& context)
        {
            const std::optional<float> pixels = Pixels(value);
            if (!pixels || *pixels < 0.0f)
            {
                return Fail(context, "it is not a length of zero or more in px");
            }
            out = *pixels;
            return true;
        }

        bool ParseNumber(std::string_view value, float& out, StyleParseContext& context)
        {
            const std::optional<float> number = PlainNumber(value);
            if (!number || *number < 0.0f)
            {
                return Fail(context, "it is not a number of zero or more");
            }
            out = *number;
            return true;
        }

        bool ParseRatio(std::string_view value, float& out, StyleParseContext& context)
        {
            value = Trim(value);
            if (EqualsIgnoringCase(value, "auto"))
            {
                out = 0.0f;
                return true;
            }
            const std::size_t slash = value.find('/');
            const std::optional<float> width = PlainNumber(value.substr(0, slash));
            const std::optional<float> height =
                slash == std::string_view::npos ? std::optional<float>(1.0f) : PlainNumber(value.substr(slash + 1));
            if (!width || !height || *width < 0.0f || *height < 0.0f)
            {
                return Fail(context, "it is not a ratio (a number, or two over a slash), or auto");
            }
            // A ratio with a zero in it is degenerate, which CSS treats as none.
            out = *width > 0.0f && *height > 0.0f ? *width / *height : 0.0f;
            return true;
        }

        bool ParseOpacity(std::string_view value, float& out, StyleParseContext& context)
        {
            const std::optional<float> alpha = AlphaValue(value);
            if (!alpha)
            {
                return Fail(context, "it is not a number or a percentage");
            }
            out = std::clamp(*alpha, 0.0f, 1.0f);
            return true;
        }

        bool ParseInteger(std::string_view value, std::int32_t& out, StyleParseContext& context)
        {
            value = Trim(value);
            if (EqualsIgnoringCase(value, "auto"))
            {
                out = 0;
                return true;
            }
            if (!value.empty() && value.front() == '+')
            {
                value.remove_prefix(1);
            }
            std::int32_t number = 0;
            const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), number);
            if (error != std::errc{} || end != value.data() + value.size() || value.empty())
            {
                return Fail(context, "it is not a whole number, or auto");
            }
            out = number;
            return true;
        }

        bool ParseColour(std::string_view value, Colour& out, StyleParseContext& context)
        {
            const std::optional<Colour> colour = ReadColour(value);
            if (!colour)
            {
                return Fail(context, "it is not a colour (#hex, rgb(), rgba(), hsl(), hsla() or a colour name)");
            }
            out = *colour;
            return true;
        }

        bool ParseImageUrl(std::string_view value, AssetHandle<Image>& out, StyleParseContext& context)
        {
            if (EqualsIgnoringCase(Trim(value), "none"))
            {
                out = {};
                return true;
            }
            const std::optional<std::string_view> path = UrlPath(value);
            if (!path)
            {
                return Fail(context, "it is not url(path) or none");
            }
            if (context.checkOnly)
            {
                return true;
            }
            const AssetHandle<Image> image = context.loadImage ? context.loadImage(*path) : AssetHandle<Image>{};
            if (image.IsEmpty())
            {
                return Fail(context, "the image " + std::string(*path) + " could not be loaded");
            }
            out = image;
            return true;
        }

        bool ParseBackgroundImage(std::string_view value, BackgroundImage& out, StyleParseContext& context)
        {
            value = Trim(value);
            if (EqualsIgnoringCase(value, "none"))
            {
                out = std::monostate{};
                return true;
            }
            if (const auto arguments = CallOf(value, "linear-gradient"))
            {
                std::vector<std::string_view> parts = Split(*arguments, ',');
                StyleLinearGradient gradient;
                if (!parts.empty())
                {
                    if (const std::optional<float> radians = Angle(parts.front()))
                    {
                        gradient.degrees = *radians * 180.0f / std::numbers::pi_v<float>;
                        parts.erase(parts.begin());
                    }
                    else if (const std::optional<float> side = SideAngle(parts.front()))
                    {
                        gradient.degrees = *side;
                        parts.erase(parts.begin());
                    }
                }
                const auto stops = ReadStops(parts);
                if (!stops)
                {
                    return Fail(context, "it is not linear-gradient([angle or to side,] colour [n%], colour [n%]...)");
                }
                gradient.stops = *stops;
                out = std::move(gradient);
                return true;
            }
            if (const auto arguments = CallOf(value, "radial-gradient"))
            {
                std::vector<std::string_view> parts = Split(*arguments, ',');
                if (!parts.empty() && EqualsIgnoringCase(parts.front(), "ellipse"))
                {
                    parts.erase(parts.begin());
                }
                const auto stops = ReadStops(parts);
                if (!stops)
                {
                    return Fail(context, "it is not radial-gradient([ellipse,] colour [n%], colour [n%]...)");
                }
                out = StyleRadialGradient{*stops};
                return true;
            }
            AssetHandle<Image> image;
            if (!ParseImageUrl(value, image, context))
            {
                if (context.error.empty() || !UrlPath(value))
                {
                    context.error = "it is not url(path), a gradient, or none";
                }
                return false;
            }
            out = image;
            return true;
        }

        bool ParseSlice(std::string_view value, Sides& out, StyleParseContext& context)
        {
            const std::vector<std::string_view> parts = Split(value, ' ');
            std::vector<float> numbers;
            for (const std::string_view part : parts)
            {
                const std::optional<float> number = PlainNumber(part);
                if (!number || *number < 0.0f)
                {
                    return Fail(context, "it is not one to four numbers of the image's pixels");
                }
                numbers.push_back(*number);
            }
            if (numbers.empty() || numbers.size() > 4)
            {
                return Fail(context, "it is not one to four numbers of the image's pixels");
            }
            // CSS's order and repetition: top, right, bottom, left.
            const float top = numbers[0];
            const float right = numbers.size() > 1 ? numbers[1] : top;
            const float bottom = numbers.size() > 2 ? numbers[2] : top;
            const float left = numbers.size() > 3 ? numbers[3] : right;
            out = {top, right, bottom, left};
            return true;
        }

        bool ParseBoxShadow(std::string_view value, std::vector<Shadow>& out, StyleParseContext& context)
        {
            if (EqualsIgnoringCase(Trim(value), "none"))
            {
                out.clear();
                return true;
            }
            std::vector<Shadow> shadows;
            for (const std::string_view layer : Split(value, ','))
            {
                Shadow shadow;
                std::vector<float> lengths;
                bool coloured = false;
                for (const std::string_view word : Split(layer, ' '))
                {
                    if (EqualsIgnoringCase(word, "inset"))
                    {
                        return Fail(context, "inset box shadows are not supported");
                    }
                    if (const std::optional<float> pixels = Pixels(word))
                    {
                        lengths.push_back(*pixels);
                    }
                    else if (const std::optional<Colour> colour = ReadColour(word); colour && !coloured)
                    {
                        shadow.colour = *colour;
                        coloured = true;
                    }
                    else
                    {
                        return Fail(context, "it is not a list of offset-x offset-y [blur [spread]] [colour]");
                    }
                }
                if (lengths.size() < 2 || lengths.size() > 4 || (lengths.size() > 2 && lengths[2] < 0.0f))
                {
                    return Fail(context, "it is not a list of offset-x offset-y [blur [spread]] [colour]");
                }
                shadow.offset = {lengths[0], lengths[1]};
                shadow.blur = lengths.size() > 2 ? lengths[2] : 0.0f;
                shadow.spread = lengths.size() > 3 ? lengths[3] : 0.0f;
                shadows.push_back(shadow);
            }
            out = std::move(shadows);
            return true;
        }

        bool ParseTransform(std::string_view value, std::vector<TransformStep>& out, StyleParseContext& context)
        {
            if (EqualsIgnoringCase(Trim(value), "none"))
            {
                out.clear();
                return true;
            }
            const auto fail = [&] {
                return Fail(context, "it is not a list of translate(), translateX(), translateY(), scale(), scaleX(), "
                                     "scaleY() and rotate(), or none");
            };
            std::vector<TransformStep> steps;
            for (const std::string_view function : Split(value, ' '))
            {
                const std::size_t open = function.find('(');
                if (open == std::string_view::npos)
                {
                    return fail();
                }
                const std::string_view name = function.substr(0, open);
                const std::optional<std::string_view> arguments = CallOf(function, name);
                if (!arguments)
                {
                    return fail();
                }
                const std::vector<std::string_view> parts = Split(*arguments, ',');
                if (parts.empty() || parts.size() > 2)
                {
                    return fail();
                }
                if (EqualsIgnoringCase(name, "translate") || EqualsIgnoringCase(name, "translateX") ||
                    EqualsIgnoringCase(name, "translateY"))
                {
                    const std::optional<Length> first = ReadLength(parts[0]);
                    const std::optional<Length> second =
                        parts.size() == 2 ? ReadLength(parts[1]) : std::optional<Length>(Length::Px(0.0f));
                    if (!first || !second || first->unit == LengthUnit::Auto || second->unit == LengthUnit::Auto ||
                        (parts.size() == 2 && !EqualsIgnoringCase(name, "translate")))
                    {
                        return fail();
                    }
                    if (EqualsIgnoringCase(name, "translateY"))
                    {
                        steps.push_back(TransformStep::Translate(Length::Px(0.0f), *first));
                    }
                    else
                    {
                        steps.push_back(TransformStep::Translate(*first, *second));
                    }
                }
                else if (EqualsIgnoringCase(name, "scale") || EqualsIgnoringCase(name, "scaleX") ||
                         EqualsIgnoringCase(name, "scaleY"))
                {
                    const std::optional<float> first = PlainNumber(parts[0]);
                    const std::optional<float> second = parts.size() == 2 ? PlainNumber(parts[1]) : first;
                    if (!first || !second || (parts.size() == 2 && !EqualsIgnoringCase(name, "scale")))
                    {
                        return fail();
                    }
                    if (EqualsIgnoringCase(name, "scaleX"))
                    {
                        steps.push_back(TransformStep::Scale({*first, 1.0f}));
                    }
                    else if (EqualsIgnoringCase(name, "scaleY"))
                    {
                        steps.push_back(TransformStep::Scale({1.0f, *first}));
                    }
                    else
                    {
                        steps.push_back(TransformStep::Scale({*first, *second}));
                    }
                }
                else if (EqualsIgnoringCase(name, "rotate"))
                {
                    const std::optional<float> radians = parts.size() == 1 ? Angle(parts[0]) : std::nullopt;
                    if (!radians)
                    {
                        return fail();
                    }
                    steps.push_back(TransformStep::Rotate(*radians));
                }
                else
                {
                    return fail();
                }
            }
            out = std::move(steps);
            return true;
        }

        /** @brief `transform-origin`: one or two lengths or keywords, x first (CSS also takes y first for keywords). */
        bool ParseTransformOrigin(std::string_view value, StyleDeclarations& into, StyleParseContext& context)
        {
            const std::vector<std::string_view> parts = Split(value, ' ');
            if (parts.empty() || parts.size() > 2)
            {
                return Fail(context, "it is not one or two lengths or keywords");
            }
            const auto read = [](std::string_view part, bool& vertical) -> std::optional<Length> {
                vertical = false;
                if (EqualsIgnoringCase(part, "left"))
                {
                    return Length::Percent(0.0f);
                }
                if (EqualsIgnoringCase(part, "right"))
                {
                    return Length::Percent(100.0f);
                }
                if (EqualsIgnoringCase(part, "center"))
                {
                    return Length::Percent(50.0f);
                }
                if (EqualsIgnoringCase(part, "top") || EqualsIgnoringCase(part, "bottom"))
                {
                    vertical = true;
                    return Length::Percent(EqualsIgnoringCase(part, "top") ? 0.0f : 100.0f);
                }
                const std::optional<Length> length = ReadLength(part);
                return length && length->unit != LengthUnit::Auto ? length : std::nullopt;
            };
            bool firstVertical = false;
            bool secondVertical = false;
            std::optional<Length> x = read(parts[0], firstVertical);
            std::optional<Length> y =
                parts.size() == 2 ? read(parts[1], secondVertical) : std::optional<Length>(Length::Percent(50.0f));
            if (!x || !y)
            {
                return Fail(context, "it is not one or two lengths or keywords");
            }
            if (firstVertical)
            {
                std::swap(x, y);
                if (parts.size() == 1)
                {
                    x = Length::Percent(50.0f);
                }
            }
            into.SetTransformOrigin(*x, *y);
            return true;
        }

        using Parser = bool (*)(std::string_view, StyleDeclarations&, StyleParseContext&);

        struct PropertyEntry
        {
            std::string_view name;
            StyleProperty property;
            Parser parse;
        };

        // One parser per property, reading into a copy of the property's field and setting it only if it was read.
#define FENRIR_UI_PROPERTY_ENTRY(Enum, Name, Field, Parse)                                          \
    PropertyEntry{Name, StyleProperty::Enum,                                                        \
                  [](std::string_view value, StyleDeclarations& into, StyleParseContext& context) { \
                      auto field = into.Values().Field;                                             \
                      if (!Parse(value, field, context))                                            \
                      {                                                                             \
                          return false;                                                             \
                      }                                                                             \
                      into.Set(StyleProperty::Enum, &ResolvedStyle::Field, std::move(field));       \
                      return true;                                                                  \
                  }},

        const std::array<PropertyEntry, StylePropertyCount> Properties{
            {FENRIR_UI_STYLE_PROPERTIES(FENRIR_UI_PROPERTY_ENTRY)}};

#undef FENRIR_UI_PROPERTY_ENTRY
    } // namespace

    bool ParseStyleProperty(std::string_view name, std::string_view value, StyleDeclarations& into,
                            StyleParseContext& context)
    {
        name = Trim(name);
        if (EqualsIgnoringCase(name, "transform-origin"))
        {
            return ParseTransformOrigin(value, into, context);
        }
        for (const PropertyEntry& entry : Properties)
        {
            // transform-origin's two parts are named only by transform-origin, as in CSS.
            if (entry.property == StyleProperty::TransformOriginX || entry.property == StyleProperty::TransformOriginY)
            {
                continue;
            }
            if (EqualsIgnoringCase(name, entry.name))
            {
                return entry.parse(value, into, context);
            }
        }
        return Fail(context, "it is not a property FenrirUI knows");
    }

    bool IsInherited(StyleProperty property) noexcept
    {
        return property == StyleProperty::PointerEvents || property == StyleProperty::Cursor;
    }

    namespace
    {
        constexpr std::array<std::string_view, 11> Shorthands{
            "margin",     "padding", "border", "border-width", "border-color", "border-radius",
            "background", "flex",    "gap",    "transition",   "animation",
        };

        // The spec's property set that the style does not hold yet: text (ticket 09), transitions and animations (10),
        // scrollbars (12), and the filters and background-position.
        constexpr std::array<std::string_view, 27> LaterLonghands{
            "color",
            "font-family",
            "font-size",
            "font-weight",
            "font-style",
            "text-align",
            "line-height",
            "letter-spacing",
            "text-shadow",
            "white-space",
            "text-overflow",
            "filter",
            "backdrop-filter",
            "background-position",
            "scrollbar-color",
            "scrollbar-width",
            "transition-property",
            "transition-duration",
            "transition-timing-function",
            "transition-delay",
            "animation-name",
            "animation-duration",
            "animation-timing-function",
            "animation-delay",
            "animation-iteration-count",
            "animation-direction",
            "animation-fill-mode",
        };

        bool IsNameChar(char c)
        {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
        }

        bool IsIdentifier(std::string_view text)
        {
            if (text.empty() || (text.front() >= '0' && text.front() <= '9'))
            {
                return false;
            }
            return std::all_of(text.begin(), text.end(), IsNameChar);
        }

        bool IsOneOf(std::string_view text, std::span<const std::string_view> words)
        {
            return std::any_of(words.begin(), words.end(),
                               [&](std::string_view w) { return EqualsIgnoringCase(text, w); });
        }

        /** @brief A time in `s` or `ms`. */
        bool IsTime(std::string_view text)
        {
            const auto number = NumberAndUnit(text);
            return number && (EqualsIgnoringCase(number->second, "s") || EqualsIgnoringCase(number->second, "ms"));
        }

        bool IsTimingFunction(std::string_view text)
        {
            constexpr std::array<std::string_view, 7> named{"ease",        "linear",     "ease-in", "ease-out",
                                                            "ease-in-out", "step-start", "step-end"};
            if (IsOneOf(text, named))
            {
                return true;
            }
            if (const auto arguments = CallOf(text, "cubic-bezier"))
            {
                const std::vector<std::string_view> parts = Split(*arguments, ',');
                return parts.size() == 4 &&
                       std::all_of(parts.begin(), parts.end(), [](std::string_view p) { return PlainNumber(p); });
            }
            if (const auto arguments = CallOf(text, "steps"))
            {
                const std::vector<std::string_view> parts = Split(*arguments, ',');
                constexpr std::array<std::string_view, 6> positions{"jump-start", "jump-end", "jump-none",
                                                                    "jump-both",  "start",    "end"};
                const std::optional<float> count = parts.empty() ? std::nullopt : PlainNumber(parts[0]);
                return count && *count >= 1.0f && std::floor(*count) == *count &&
                       (parts.size() == 1 || (parts.size() == 2 && IsOneOf(parts[1], positions)));
            }
            return false;
        }

        /** @brief Joins parts with a space. */
        std::string Join(std::span<const std::string_view> parts, std::string_view separator = " ")
        {
            std::string out;
            for (std::size_t i = 0; i < parts.size(); ++i)
            {
                if (i > 0)
                {
                    out += separator;
                }
                out += parts[i];
            }
            return out;
        }

        /** @brief One to four values round the box, top first, as margin, padding and the border lists take them. */
        bool ExpandSides(std::string_view value, const std::array<std::string_view, 4>& names,
                         std::vector<Longhand>& out, StyleParseContext& context)
        {
            const std::vector<std::string_view> parts = Split(value, ' ');
            if (parts.empty() || parts.size() > 4)
            {
                return Fail(context, "it is not one to four values");
            }
            const std::size_t n = parts.size();
            // CSS: top; then right defaults to top, bottom to top, left to right.
            const std::array<std::string_view, 4> sides{parts[0], n > 1 ? parts[1] : parts[0],
                                                        n > 2 ? parts[2] : parts[0],
                                                        n > 3 ? parts[3] : (n > 1 ? parts[1] : parts[0])};
            for (std::size_t i = 0; i < 4; ++i)
            {
                out.emplace_back(names[i], sides[i]);
            }
            return true;
        }

        bool ExpandBorder(std::string_view value, std::vector<Longhand>& out, StyleParseContext& context)
        {
            constexpr std::array<std::string_view, 10> styles{"none",   "hidden", "dotted", "dashed", "solid",
                                                              "double", "groove", "ridge",  "inset",  "outset"};
            std::optional<std::string> width;
            std::optional<std::string_view> style;
            std::optional<std::string_view> colour;
            for (const std::string_view part : Split(value, ' '))
            {
                if (!style && IsOneOf(part, styles))
                {
                    style = part;
                }
                else if (!width &&
                         (Pixels(part) || IsOneOf(part, std::array<std::string_view, 3>{"thin", "medium", "thick"})))
                {
                    width = EqualsIgnoringCase(part, "thin")     ? "1px"
                            : EqualsIgnoringCase(part, "medium") ? "3px"
                            : EqualsIgnoringCase(part, "thick")  ? "5px"
                                                                 : std::string(part);
                }
                else if (!colour && ReadColour(part))
                {
                    colour = part;
                }
                else
                {
                    return Fail(context, "it is not a width, a style and a colour, each at most once");
                }
            }
            if (!width && !style && !colour)
            {
                return Fail(context, "it is empty");
            }
            // CSS computes a border's width to zero when its style is none or hidden, which it is when left out.
            const bool drawn = style && !EqualsIgnoringCase(*style, "none") && !EqualsIgnoringCase(*style, "hidden");
            const std::string widthText = drawn ? width.value_or("3px") : "0";
            for (const std::string_view side : {"top", "right", "bottom", "left"})
            {
                out.emplace_back("border-" + std::string(side) + "-width", widthText);
            }
            // CSS's initial border colour is currentcolor; the style has no color property yet, and its border colours
            // start black.
            for (const std::string_view side : {"top", "right", "bottom", "left"})
            {
                out.emplace_back("border-" + std::string(side) + "-color", std::string(colour.value_or("black")));
            }
            return true;
        }

        bool ExpandBackground(std::string_view value, std::vector<Longhand>& out, StyleParseContext& context)
        {
            if (Split(value, ',').size() > 1)
            {
                return Fail(context, "it has more than one layer");
            }
            // The size is the one or two values that follow a slash at the top level, after the position.
            std::vector<std::string_view> parts;
            std::vector<std::string_view> sizeParts;
            bool slash = false;
            int depth = 0;
            for (std::size_t i = 0; i < value.size(); ++i)
            {
                depth += value[i] == '(' ? 1 : value[i] == ')' ? -1 : 0;
                if (depth == 0 && value[i] == '/')
                {
                    slash = true;
                    parts = Split(value.substr(0, i), ' ');
                    const std::vector<std::string_view> after = Split(value.substr(i + 1), ' ');
                    constexpr std::array<std::string_view, 4> sizeWords{"cover", "contain", "stretch", "auto"};
                    std::size_t next = 0;
                    while (next < after.size() && sizeParts.size() < 2 &&
                           (IsOneOf(after[next], sizeWords) ||
                            (ReadLength(after[next]) && !EqualsIgnoringCase(after[next], "none"))))
                    {
                        sizeParts.push_back(after[next++]);
                    }
                    parts.insert(parts.end(), after.begin() + static_cast<std::ptrdiff_t>(next), after.end());
                    break;
                }
            }
            if (!slash)
            {
                parts = Split(value, ' ');
            }
            std::optional<std::string_view> colour;
            std::optional<std::string_view> image;
            std::vector<std::string_view> position;
            constexpr std::array<std::string_view, 5> positionWords{"left", "right", "top", "bottom", "center"};
            for (const std::string_view part : parts)
            {
                if (!image && (EqualsIgnoringCase(part, "none") || UrlPath(part) || CallOf(part, "linear-gradient") ||
                               CallOf(part, "radial-gradient")))
                {
                    image = part;
                }
                else if (IsOneOf(part, positionWords) ||
                         (ReadLength(part) && !EqualsIgnoringCase(part, "auto") && !EqualsIgnoringCase(part, "none")))
                {
                    position.push_back(part);
                }
                else if (!colour && ReadColour(part))
                {
                    colour = part;
                }
                else
                {
                    return Fail(context, "'" + std::string(part) +
                                             "' is not a colour, an image or a position (FenrirUI's background takes "
                                             "background-color, -image, -position and -size)");
                }
            }
            if (slash && (position.empty() || sizeParts.empty()))
            {
                return Fail(context, "a size after a slash needs a position before it");
            }
            out.emplace_back("background-color", std::string(colour.value_or("transparent")));
            out.emplace_back("background-image", std::string(image.value_or("none")));
            out.emplace_back("background-position", position.empty() ? std::string("0% 0%") : Join(position));
            out.emplace_back("background-size", sizeParts.empty() ? std::string("stretch") : Join(sizeParts));
            return true;
        }

        bool ExpandFlex(std::string_view value, std::vector<Longhand>& out, StyleParseContext& context)
        {
            const auto emit = [&](std::string grow, std::string shrink, std::string basis) {
                out.emplace_back("flex-grow", std::move(grow));
                out.emplace_back("flex-shrink", std::move(shrink));
                out.emplace_back("flex-basis", std::move(basis));
                return true;
            };
            const std::string_view trimmed = Trim(value);
            if (EqualsIgnoringCase(trimmed, "none"))
            {
                return emit("0", "0", "auto");
            }
            if (EqualsIgnoringCase(trimmed, "auto"))
            {
                return emit("1", "1", "auto");
            }
            if (EqualsIgnoringCase(trimmed, "initial"))
            {
                return emit("0", "1", "auto");
            }
            std::vector<std::string> numbers;
            std::optional<std::string> basis;
            for (const std::string_view part : Split(value, ' '))
            {
                if (PlainNumber(part) && numbers.size() < 2)
                {
                    numbers.emplace_back(part);
                }
                else if (!basis && ReadLength(part) && !EqualsIgnoringCase(part, "none"))
                {
                    basis = std::string(part);
                }
                else
                {
                    return Fail(context, "it is not none, auto, or up to two numbers and a basis");
                }
            }
            if (numbers.empty() && !basis)
            {
                return Fail(context, "it is empty");
            }
            // CSS: a grow given alone makes the basis 0%; a shrink left out is 1.
            return emit(numbers.empty() ? "1" : numbers[0], numbers.size() > 1 ? numbers[1] : "1",
                        basis.value_or("0%"));
        }

        bool ExpandGap(std::string_view value, std::vector<Longhand>& out, StyleParseContext& context)
        {
            const std::vector<std::string_view> parts = Split(value, ' ');
            if (parts.empty() || parts.size() > 2)
            {
                return Fail(context, "it is not one or two lengths");
            }
            out.emplace_back("row-gap", std::string(parts[0]));
            out.emplace_back("column-gap", std::string(parts.size() > 1 ? parts[1] : parts[0]));
            return true;
        }

        bool ExpandTransition(std::string_view value, std::vector<Longhand>& out, StyleParseContext& context)
        {
            std::vector<std::string> properties;
            std::vector<std::string> durations;
            std::vector<std::string> timings;
            std::vector<std::string> delays;
            for (const std::string_view single : Split(value, ','))
            {
                std::optional<std::string_view> property;
                std::optional<std::string_view> duration;
                std::optional<std::string_view> delay;
                std::optional<std::string_view> timing;
                for (const std::string_view part : Split(single, ' '))
                {
                    if (IsTime(part) && !duration)
                    {
                        duration = part;
                    }
                    else if (IsTime(part) && !delay)
                    {
                        delay = part;
                    }
                    else if (!timing && IsTimingFunction(part))
                    {
                        timing = part;
                    }
                    else if (!property && IsIdentifier(part))
                    {
                        property = part;
                    }
                    else
                    {
                        return Fail(context, "'" + std::string(part) +
                                                 "' is not a property, a time or a timing "
                                                 "function, each at most once (two times)");
                    }
                }
                if (!property && !duration && !timing)
                {
                    return Fail(context, "a transition in its list is empty");
                }
                properties.emplace_back(property.value_or("all"));
                durations.emplace_back(duration.value_or("0s"));
                timings.emplace_back(timing.value_or("ease"));
                delays.emplace_back(delay.value_or("0s"));
            }
            const auto list = [](const std::vector<std::string>& items) {
                std::vector<std::string_view> views(items.begin(), items.end());
                return Join(views, ", ");
            };
            out.emplace_back("transition-property", list(properties));
            out.emplace_back("transition-duration", list(durations));
            out.emplace_back("transition-timing-function", list(timings));
            out.emplace_back("transition-delay", list(delays));
            return true;
        }

        bool ExpandAnimation(std::string_view value, std::vector<Longhand>& out, StyleParseContext& context)
        {
            constexpr std::array<std::string_view, 4> directions{"normal", "reverse", "alternate", "alternate-reverse"};
            constexpr std::array<std::string_view, 4> fillModes{"none", "forwards", "backwards", "both"};
            constexpr std::array<std::string_view, 2> playStates{"running", "paused"};
            std::array<std::vector<std::string>, 7> lists;
            for (const std::string_view single : Split(value, ','))
            {
                std::array<std::optional<std::string>, 7> found;
                enum Slot : std::size_t
                {
                    Name,
                    Duration,
                    Timing,
                    Delay,
                    Count,
                    Direction,
                    Fill,
                };
                for (const std::string_view part : Split(single, ' '))
                {
                    const std::optional<float> number = PlainNumber(part);
                    if (IsTime(part) && !found[Duration])
                    {
                        found[Duration] = std::string(part);
                    }
                    else if (IsTime(part) && !found[Delay])
                    {
                        found[Delay] = std::string(part);
                    }
                    else if (!found[Timing] && IsTimingFunction(part))
                    {
                        found[Timing] = std::string(part);
                    }
                    else if (!found[Count] && (EqualsIgnoringCase(part, "infinite") || (number && *number >= 0.0f)))
                    {
                        found[Count] = std::string(part);
                    }
                    else if (!found[Direction] && IsOneOf(part, directions))
                    {
                        found[Direction] = std::string(part);
                    }
                    else if (!found[Fill] && IsOneOf(part, fillModes))
                    {
                        found[Fill] = std::string(part);
                    }
                    else if (IsOneOf(part, playStates))
                    {
                        // animation-play-state is not in the property set; an animation always runs.
                    }
                    else if (!found[Name] && IsIdentifier(part))
                    {
                        found[Name] = std::string(part);
                    }
                    else
                    {
                        return Fail(context, "'" + std::string(part) + "' is not part of an animation, or repeats one");
                    }
                }
                constexpr std::array<std::string_view, 7> initial{"none", "0s", "ease", "0s", "1", "normal", "none"};
                for (std::size_t i = 0; i < 7; ++i)
                {
                    lists[i].push_back(found[i].value_or(std::string(initial[i])));
                }
            }
            constexpr std::array<std::string_view, 7> names{
                "animation-name",     "animation-duration",        "animation-timing-function",
                "animation-delay",    "animation-iteration-count", "animation-direction",
                "animation-fill-mode"};
            for (std::size_t i = 0; i < 7; ++i)
            {
                std::vector<std::string_view> views(lists[i].begin(), lists[i].end());
                out.emplace_back(names[i], Join(views, ", "));
            }
            return true;
        }

        /** @brief The end of the `var(` call starting at `start` (its `)`), or npos when it does not close. */
        std::size_t CallEnd(std::string_view text, std::size_t open)
        {
            int depth = 0;
            for (std::size_t i = open; i < text.size(); ++i)
            {
                if (text[i] == '(')
                {
                    ++depth;
                }
                else if (text[i] == ')' && --depth == 0)
                {
                    return i;
                }
            }
            return std::string_view::npos;
        }

        /** @brief Where the next `var(` is at or after `from`, or npos. */
        std::size_t FindVar(std::string_view text, std::size_t from)
        {
            for (std::size_t i = from; i + 4 <= text.size(); ++i)
            {
                const bool boundary = i == 0 || !IsNameChar(text[i - 1]);
                if (boundary && EqualsIgnoringCase(text.substr(i, 4), "var("))
                {
                    return i;
                }
            }
            return std::string_view::npos;
        }

        /** @brief A `var()`'s name and its fallback (nothing when it has none), or nothing when it is malformed. */
        std::optional<std::pair<std::string_view, std::optional<std::string_view>>> VarArguments(std::string_view inner)
        {
            const std::size_t comma = inner.find(',');
            const std::string_view name = Trim(inner.substr(0, comma));
            if (name.size() < 3 || name.substr(0, 2) != "--" || !IsIdentifier(name.substr(2)))
            {
                return std::nullopt;
            }
            std::optional<std::string_view> fallback;
            if (comma != std::string_view::npos)
            {
                fallback = Trim(inner.substr(comma + 1));
            }
            return std::pair{name, fallback};
        }

        std::optional<std::string> Substitute(std::string_view value,
                                              const std::function<const std::string*(std::string_view)>& lookup,
                                              int depth)
        {
            // A custom property that refers to itself, directly or round a loop, is a cycle CSS makes invalid.
            if (depth > 16)
            {
                return std::nullopt;
            }
            std::string out;
            std::size_t at = 0;
            for (std::size_t start = FindVar(value, 0); start != std::string_view::npos; start = FindVar(value, at))
            {
                const std::size_t end = CallEnd(value, start + 3);
                if (end == std::string_view::npos)
                {
                    return std::nullopt;
                }
                const auto arguments = VarArguments(value.substr(start + 4, end - start - 4));
                if (!arguments)
                {
                    return std::nullopt;
                }
                out += value.substr(at, start - at);
                std::optional<std::string> replacement;
                if (const std::string* found = lookup(arguments->first))
                {
                    replacement = Substitute(*found, lookup, depth + 1);
                }
                if (!replacement && arguments->second)
                {
                    replacement = Substitute(*arguments->second, lookup, depth + 1);
                }
                if (!replacement)
                {
                    return std::nullopt;
                }
                out += *replacement;
                at = end + 1;
            }
            out += value.substr(at);
            return out;
        }
    } // namespace

    PropertyKind ClassifyProperty(std::string_view name) noexcept
    {
        if (std::find(Shorthands.begin(), Shorthands.end(), name) != Shorthands.end())
        {
            return PropertyKind::Shorthand;
        }
        if (std::find(LaterLonghands.begin(), LaterLonghands.end(), name) != LaterLonghands.end())
        {
            return PropertyKind::NotYetStyled;
        }
        if (name == "transform-origin")
        {
            return PropertyKind::Styled;
        }
        for (const PropertyEntry& entry : Properties)
        {
            if (entry.property != StyleProperty::TransformOriginX &&
                entry.property != StyleProperty::TransformOriginY && entry.name == name)
            {
                return PropertyKind::Styled;
            }
        }
        return PropertyKind::Unknown;
    }

    bool ExpandShorthand(std::string_view name, std::string_view value, std::vector<Longhand>& out,
                         StyleParseContext& context)
    {
        value = Trim(value);
        if (value.empty())
        {
            return Fail(context, "it is empty");
        }
        std::vector<Longhand> expanded;
        bool read = false;
        if (name == "margin")
        {
            read =
                ExpandSides(value, {"margin-top", "margin-right", "margin-bottom", "margin-left"}, expanded, context);
        }
        else if (name == "padding")
        {
            read = ExpandSides(value, {"padding-top", "padding-right", "padding-bottom", "padding-left"}, expanded,
                               context);
        }
        else if (name == "border-width")
        {
            read = ExpandSides(value,
                               {"border-top-width", "border-right-width", "border-bottom-width", "border-left-width"},
                               expanded, context);
        }
        else if (name == "border-color")
        {
            read = ExpandSides(value,
                               {"border-top-color", "border-right-color", "border-bottom-color", "border-left-color"},
                               expanded, context);
        }
        else if (name == "border-radius")
        {
            if (value.find('/') != std::string_view::npos)
            {
                return Fail(context, "elliptical radii (a slash) are not supported");
            }
            read = ExpandSides(value,
                               {"border-top-left-radius", "border-top-right-radius", "border-bottom-right-radius",
                                "border-bottom-left-radius"},
                               expanded, context);
        }
        else if (name == "border")
        {
            read = ExpandBorder(value, expanded, context);
        }
        else if (name == "background")
        {
            read = ExpandBackground(value, expanded, context);
        }
        else if (name == "flex")
        {
            read = ExpandFlex(value, expanded, context);
        }
        else if (name == "gap")
        {
            read = ExpandGap(value, expanded, context);
        }
        else if (name == "transition")
        {
            read = ExpandTransition(value, expanded, context);
        }
        else if (name == "animation")
        {
            read = ExpandAnimation(value, expanded, context);
        }
        else
        {
            return Fail(context, "it is not a shorthand");
        }
        if (read)
        {
            out.insert(out.end(), expanded.begin(), expanded.end());
        }
        return read;
    }

    bool ParseStyleDeclaration(std::string_view name, std::string_view value, StyleDeclarations& into,
                               StyleParseContext& context)
    {
        if (Trim(value).empty())
        {
            return Fail(context, "it has no value");
        }
        switch (ClassifyProperty(name))
        {
        case PropertyKind::Styled:
            return ParseStyleProperty(name, value, into, context);
        case PropertyKind::NotYetStyled:
            return true;
        case PropertyKind::Shorthand: {
            std::vector<Longhand> longhands;
            if (!ExpandShorthand(name, value, longhands, context))
            {
                return false;
            }
            StyleDeclarations read = into;
            for (const auto& [longhand, part] : longhands)
            {
                if (ClassifyProperty(longhand) == PropertyKind::Styled &&
                    !ParseStyleProperty(longhand, part, read, context))
                {
                    context.error = longhand + ": " + context.error;
                    return false;
                }
            }
            into = std::move(read);
            return true;
        }
        case PropertyKind::Unknown:
            break;
        }
        return Fail(context, "it is not a property FenrirUI knows");
    }

    std::vector<StyleProperty> PropertiesOf(std::string_view name)
    {
        if (name == "transform-origin")
        {
            return {StyleProperty::TransformOriginX, StyleProperty::TransformOriginY};
        }
        std::vector<std::string> longhands{std::string(name)};
        if (ClassifyProperty(name) == PropertyKind::Shorthand)
        {
            // Expanding a value the shorthand reads names the longhands it gives.
            std::string_view sample = "none";
            if (name == "border-color")
            {
                sample = "black";
            }
            else if (name == "border")
            {
                sample = "solid";
            }
            else if (name != "background" && name != "flex" && name != "transition" && name != "animation")
            {
                sample = "0";
            }
            StyleParseContext context;
            std::vector<Longhand> expanded;
            ExpandShorthand(name, sample, expanded, context);
            longhands.clear();
            for (Longhand& longhand : expanded)
            {
                longhands.push_back(std::move(longhand.first));
            }
        }
        std::vector<StyleProperty> properties;
        for (const std::string& longhand : longhands)
        {
            for (const PropertyEntry& entry : Properties)
            {
                if (entry.name == longhand && entry.property != StyleProperty::TransformOriginX &&
                    entry.property != StyleProperty::TransformOriginY)
                {
                    properties.push_back(entry.property);
                }
            }
        }
        return properties;
    }

    bool ContainsVar(std::string_view value) noexcept
    {
        return FindVar(value, 0) != std::string_view::npos;
    }

    bool CheckVarSyntax(std::string_view value, std::string& error)
    {
        for (std::size_t start = FindVar(value, 0); start != std::string_view::npos;)
        {
            const std::size_t end = CallEnd(value, start + 3);
            if (end == std::string_view::npos)
            {
                error = "a var() is not closed";
                return false;
            }
            const auto arguments = VarArguments(value.substr(start + 4, end - start - 4));
            if (!arguments)
            {
                error = "a var() does not name a custom property (--name)";
                return false;
            }
            if (arguments->second && !CheckVarSyntax(*arguments->second, error))
            {
                return false;
            }
            start = FindVar(value, end + 1);
        }
        return true;
    }

    std::optional<std::string> SubstituteVars(std::string_view value,
                                              const std::function<const std::string*(std::string_view)>& lookup)
    {
        return Substitute(value, lookup, 0);
    }
} // namespace Fenrir::Detail
