#include "FenrirDotNetHost/DotNetHost.hpp"

#include <nethost.h>

#include <cstdint>
#include <iomanip>
#include <sstream>
#include <type_traits>
#include <vector>

namespace Fenrir::Scripting
{
    namespace
    {
        // The nethost contract documents this status; keep hosting details private.
        constexpr std::uint32_t HostApiBufferTooSmall = 0x80008098U;

        std::string DiscoveryFailure(int status)
        {
            std::ostringstream diagnostic;
            diagnostic << "get_hostfxr_path failed with status 0x" << std::hex << std::uppercase << std::setfill('0')
                       << std::setw(8) << static_cast<std::uint32_t>(status) << '.';
            return diagnostic.str();
        }
    } // namespace

    HostFxrDiscoveryResult DotNetHost::DiscoverHostFxr(const std::filesystem::path& dotnetRoot) const
    {
        // Windows uses wchar_t; Unix uses char. Do not narrow native Windows paths.
        static_assert(std::is_same_v<std::filesystem::path::value_type, char_t>);

        if (!dotnetRoot.empty() && !dotnetRoot.is_absolute())
        {
            return {false, {}, "An explicit .NET root must be an absolute path."};
        }

        const get_hostfxr_parameters parameters{sizeof(get_hostfxr_parameters), nullptr, dotnetRoot.c_str()};
        const auto* searchParameters = dotnetRoot.empty() ? nullptr : &parameters;

        // Query the required size first, in char_t units including the null terminator.
        // Retry resizing if the installation changes between calls, with a finite limit.
        std::vector<char_t> buffer;
        for (int attempt = 0; attempt < 3; ++attempt)
        {
            std::size_t requiredSize = buffer.size();
            const int status =
                get_hostfxr_path(buffer.empty() ? nullptr : buffer.data(), &requiredSize, searchParameters);
            if (status == 0)
            {
                if (buffer.empty() || buffer.front() == char_t{})
                {
                    return {false, {}, "get_hostfxr_path returned an empty path."};
                }
                return {true, std::filesystem::path(buffer.data()), {}};
            }

            if (static_cast<std::uint32_t>(status) != HostApiBufferTooSmall)
            {
                return {false, {}, DiscoveryFailure(status)};
            }

            if (requiredSize <= buffer.size())
            {
                return {false, {}, "get_hostfxr_path did not provide a larger required buffer size."};
            }
            buffer.resize(requiredSize);
        }

        return {false, {}, "get_hostfxr_path repeatedly changed its required buffer size (status 0x80008098)."};
    }
} // namespace Fenrir::Scripting
