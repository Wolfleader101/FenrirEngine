// FenrirUI keeps Yoga behind its layout bridge (ADR-0064): only src/LayoutBridge.cpp includes it, and it is linked
// PRIVATE. This file includes FenrirUI's headers before anything else, so if one of them ever reaches for Yoga, or
// Yoga's include path reaches a user of FenrirUI (this test target is one), the build fails here.
#include "FenrirUI/Element.hpp"
#include "FenrirUI/ElementId.hpp"
#include "FenrirUI/Style.hpp"
#include "FenrirUI/Stylesheet.hpp"
#include "FenrirUI/UI.hpp"
#include "FenrirUI/UIEvent.hpp"
#include "FenrirUI/UIRoot.hpp"
#include "FenrirUI/UITarget.hpp"

#if defined(YG_EXPORT) || defined(YOGA_YGMACROS_H)
#error "A FenrirUI header includes Yoga."
#endif

#if __has_include(<yoga/Yoga.h>)
#error "Yoga's include path reaches a user of FenrirUI."
#endif

#include <catch2/catch_test_macros.hpp>

TEST_CASE("FenrirUI's headers compile without Yoga", "[ui][seam]")
{
    const Fenrir::ElementId none;
    INFO("a default element id names nothing");
    REQUIRE_FALSE(none.IsValid());
}
