// The watch's interface names no EnTT type (ADR-0008): its header compiles on its own without bringing EnTT in. This
// file includes nothing else first, so if the header ever reaches for EnTT the build fails here.
#include "FenrirECS/ComponentWatch.hpp"

#ifdef ENTT_VERSION_MAJOR
#error "FenrirECS/ComponentWatch.hpp must not include EnTT."
#endif

#include <catch2/catch_test_macros.hpp>

TEST_CASE("a default watch watches nothing", "[watch]")
{
    Fenrir::ComponentWatch watch;

    INFO("made without a list, it drains empty");
    REQUIRE(watch.Drain().empty());
}
