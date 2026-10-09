#include "FenrirDotNetHost/DotNetHost.hpp"

namespace Fenrir::Scripting
{
    DotNetHost::DotNetHost() noexcept = default;

    DotNetHost::~DotNetHost() noexcept = default;

    InitializationResult DotNetHost::Initialize(const HostConfiguration&)
    {
        return {false, "CoreCLR hosting is not implemented yet."};
    }

    bool DotNetHost::IsInitialized() const noexcept
    {
        return false;
    }
} // namespace Fenrir::Scripting
