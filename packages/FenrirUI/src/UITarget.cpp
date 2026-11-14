#include "FenrirUI/UITarget.hpp"

#include "FenrirUI/UIRoot.hpp"

namespace Fenrir
{
    ScaleRule UITarget::GetDefaultScaleRule() const noexcept
    {
        return ScaleRule::Height1080;
    }
} // namespace Fenrir
