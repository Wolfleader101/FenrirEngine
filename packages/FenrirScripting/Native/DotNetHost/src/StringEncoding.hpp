#pragma once

#include <hostfxr.h>

#include <string>
#include <string_view>

namespace Fenrir::Scripting::Detail
{
    // Private SDK representation: UTF-16 on Windows, UTF-8 on Unix.
    // These aliases are not managed ABI types and must stay out of public headers.
    using HostString = std::basic_string<char_t>;
    using HostStringView = std::basic_string_view<char_t>;

    // Borrow explicit-length input only during the call; return owning storage.
    // Empty input succeeds. Embedded nulls and BOM code points are preserved.
    // No replacement characters, normalization, or payload terminator is added.
    // Invalid encoding throws std::invalid_argument. Windows lengths above INT_MAX
    // throw std::length_error; other Windows API failures throw std::system_error.
    // Allocation failures retain normal C++ exception behavior.
    [[nodiscard]] HostString Utf8ToHostString(std::string_view input);
    [[nodiscard]] std::string HostStringToUtf8(HostStringView input);
} // namespace Fenrir::Scripting::Detail
