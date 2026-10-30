#include <cstring>
#include <format>

#include "FenrirLogger/ILogger.hpp"
#include "FenrirSkeleton/BoneAttachment.hpp"

namespace Fenrir
{
    bool BoneName::Set(std::string_view name, ILogger& logger)
    {
        if (name.size() > MaxBytes)
        {
            logger.Error("{}", std::format("BoneName::Set refused a name of {} bytes, over the {}-byte limit; the name "
                                           "is unchanged (never truncated, which could name another bone)",
                                           name.size(), MaxBytes));
            return false;
        }
        if (name.find('\0') != std::string_view::npos)
        {
            logger.Error("{}", "BoneName::Set refused a name holding a zero byte; the name is unchanged");
            return false;
        }
        std::memset(m_bytes, 0, sizeof(m_bytes));
        std::memcpy(m_bytes, name.data(), name.size());
        return true;
    }

    std::optional<std::string_view> BoneName::Get() const noexcept
    {
        if (!IsTerminated())
        {
            return std::nullopt;
        }
        return std::string_view(m_bytes);
    }

    bool BoneName::IsTerminated() const noexcept
    {
        return std::memchr(m_bytes, 0, sizeof(m_bytes)) != nullptr;
    }
} // namespace Fenrir
