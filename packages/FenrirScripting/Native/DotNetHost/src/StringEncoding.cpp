#include "StringEncoding.hpp"

#include <stdexcept>
#include <type_traits>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <limits>
#include <system_error>
#endif

namespace Fenrir::Scripting::Detail
{
    namespace
    {
#if defined(_WIN32)
        int WindowsLength(std::size_t length)
        {
            if (length > static_cast<std::size_t>(std::numeric_limits<int>::max()))
            {
                throw std::length_error("The text exceeds the Windows conversion API length limit.");
            }
            return static_cast<int>(length);
        }

        [[noreturn]] void ConversionFailed(const char* operation, DWORD error)
        {
            if (error == ERROR_NO_UNICODE_TRANSLATION)
            {
                throw std::invalid_argument("The text contains an invalid Unicode encoding.");
            }
            throw std::system_error(static_cast<int>(error), std::system_category(), operation);
        }
#else
        void ValidateUtf8(std::string_view input)
        {
            // Unicode Table 3-7: shortest-form sequences for scalar values only.
            // Special second-byte ranges exclude overlong forms, surrogate code
            // points, and values above U+10FFFF. Noncharacters remain valid scalars.
            std::size_t position = 0;
            while (position < input.size())
            {
                const auto lead = static_cast<unsigned char>(input[position++]);
                if (lead <= 0x7F)
                {
                    continue;
                }

                std::size_t continuationCount;
                unsigned char secondMin = 0x80;
                unsigned char secondMax = 0xBF;
                if (lead >= 0xC2 && lead <= 0xDF)
                {
                    continuationCount = 1;
                }
                else if (lead >= 0xE0 && lead <= 0xEF)
                {
                    continuationCount = 2;
                    if (lead == 0xE0)
                    {
                        secondMin = 0xA0;
                    }
                    else if (lead == 0xED)
                    {
                        secondMax = 0x9F;
                    }
                }
                else if (lead >= 0xF0 && lead <= 0xF4)
                {
                    continuationCount = 3;
                    if (lead == 0xF0)
                    {
                        secondMin = 0x90;
                    }
                    else if (lead == 0xF4)
                    {
                        secondMax = 0x8F;
                    }
                }
                else
                {
                    throw std::invalid_argument("The text contains invalid UTF-8.");
                }

                if (input.size() - position < continuationCount)
                {
                    throw std::invalid_argument("The text contains truncated UTF-8.");
                }
                for (std::size_t index = 0; index < continuationCount; ++index)
                {
                    const auto next = static_cast<unsigned char>(input[position++]);
                    const unsigned char minimum = index == 0 ? secondMin : 0x80;
                    const unsigned char maximum = index == 0 ? secondMax : 0xBF;
                    if (next < minimum || next > maximum)
                    {
                        throw std::invalid_argument("The text contains invalid UTF-8.");
                    }
                }
            }
        }
#endif
    } // namespace

    HostString Utf8ToHostString(std::string_view input)
    {
        if (input.empty())
        {
            return {};
        }
#if defined(_WIN32)
        static_assert(std::is_same_v<char_t, wchar_t>);
        const int inputLength = WindowsLength(input.size());
        const int required = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input.data(), inputLength, nullptr, 0);
        if (required == 0)
        {
            ConversionFailed("MultiByteToWideChar sizing", GetLastError());
        }
        HostString output(static_cast<std::size_t>(required), char_t{});
        const int written = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input.data(), inputLength,
                                                output.data(), required);
        if (written == 0)
        {
            ConversionFailed("MultiByteToWideChar conversion", GetLastError());
        }
        if (written != required)
        {
            throw std::runtime_error("The UTF-8 to host-string conversion returned an inconsistent length.");
        }
        return output;
#else
        static_assert(std::is_same_v<char_t, char>);
        ValidateUtf8(input);
        return HostString(input);
#endif
    }

    std::string HostStringToUtf8(HostStringView input)
    {
        if (input.empty())
        {
            return {};
        }
#if defined(_WIN32)
        static_assert(std::is_same_v<char_t, wchar_t>);
        const int inputLength = WindowsLength(input.size());
        const int required = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, input.data(), inputLength,
                                                 nullptr, 0, nullptr, nullptr);
        if (required == 0)
        {
            ConversionFailed("WideCharToMultiByte sizing", GetLastError());
        }
        std::string output(static_cast<std::size_t>(required), '\0');
        const int written = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, input.data(), inputLength,
                                                output.data(), required, nullptr, nullptr);
        if (written == 0)
        {
            ConversionFailed("WideCharToMultiByte conversion", GetLastError());
        }
        if (written != required)
        {
            throw std::runtime_error("The host-string to UTF-8 conversion returned an inconsistent length.");
        }
        return output;
#else
        static_assert(std::is_same_v<char_t, char>);
        ValidateUtf8(input);
        return std::string(input);
#endif
    }
} // namespace Fenrir::Scripting::Detail
