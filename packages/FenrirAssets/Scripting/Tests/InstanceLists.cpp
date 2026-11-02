// Instance lists from C# (lighting spec ticket 22, ADR-0051): C# adds one from spans of transforms and colours,
// replaces it, bumping the revision C++ sees, and reads it back into spans; colours that are not one per transform are
// refused, as C++'s store refuses them.

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

#include "AssetsScriptingApp.hpp"
#include "FenrirAssets/InstanceListData.hpp"

using namespace Fenrir;
using namespace Fenrir::Testing;

namespace
{
    constexpr const char* InstanceListCallsType = "AssetsScriptFixture.InstanceListCalls";

    // Copy i's place, as the fixture puts it: (i, y, 0) in a transform otherwise the identity.
    Math::Mat4 CopyAt(int i, float y)
    {
        Math::Mat4 transform(1.0f);
        transform[3] = Math::Vec4(static_cast<float>(i), y, 0.0f, 1.0f);
        return transform;
    }

    // Exactly the same channels: they crossed between C++ and C# unchanged.
    bool Same(const Colour& a, const Colour& b)
    {
        const std::equal_to<float> equal;
        return equal(a.r, b.r) && equal(a.g, b.g) && equal(a.b, b.b) && equal(a.a, b.a);
    }
} // namespace

TEST_CASE("C# adds an instance list of transforms and colours that C++ reads, then replaces it, bumping its revision",
          "[assets][scripting]")
{
    AssetsScriptingApp test;
    const auto list =
        HandleFrom<InstanceList>(test.Call<std::uint64_t>(InstanceListCallsType, "Add", 3, 3, sceneOwner));
    {
        const InstanceListData* added = test.Store().TryGet(list);
        INFO("C#'s list of three copies at (i, 0, 0), coloured (i / 4, 0.5, 1 - i / 4), was not added as C# made it");
        REQUIRE(added != nullptr);
        REQUIRE(added->transforms.size() == 3);
        REQUIRE(added->colours.size() == 3);
        for (int i = 0; i < 3; ++i)
        {
            REQUIRE(added->transforms.at(static_cast<std::size_t>(i)) == CopyAt(i, 0.0f));
            const float quarter = static_cast<float>(i) / 4.0f;
            REQUIRE(Same(added->colours.at(static_cast<std::size_t>(i)), {quarter, 0.5f, 1.0f - quarter, 1.0f}));
        }
        REQUIRE(test.Store().Revision(list) == 1);
    }

    {
        INFO("C#'s replace with two white copies at height 2 did not succeed");
        REQUIRE(test.Call<bool>(InstanceListCallsType, "Replace", BitsOf(list), 2, 2.0f, -1));
    }
    const InstanceListData* replaced = test.Store().TryGet(list);
    INFO("The replaced list is not two white copies at (i, 2, 0) with its revision bumped to 2");
    REQUIRE(replaced != nullptr);
    REQUIRE(replaced->transforms.size() == 2);
    REQUIRE(replaced->transforms.at(1) == CopyAt(1, 2.0f));
    REQUIRE(replaced->colours.empty());
    REQUIRE(test.Store().Revision(list) == 2);
}

TEST_CASE("C# reads an instance list C++ made into spans, as many as fit, with the whole count", "[assets][scripting]")
{
    AssetsScriptingApp test;
    InstanceListData data;
    data.transforms = {CopyAt(0, 1.0f), CopyAt(1, 1.0f), CopyAt(2, 1.0f)};
    data.colours = {{1.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f, 0.5f}};
    const AssetHandle<InstanceList> list = test.Store().Add<InstanceList>(data, test.Get().GetActiveScene());

    {
        INFO("C# should read every transform into a span with room, and two into a span of two, with the count 3");
        REQUIRE(Text(test.scripting, InstanceListCallsType, "ReadTransforms", BitsOf(list), 8) ==
                "3: (0, 1, 0) (1, 1, 0) (2, 1, 0)");
        REQUIRE(Text(test.scripting, InstanceListCallsType, "ReadTransforms", BitsOf(list), 2) ==
                "3: (0, 1, 0) (1, 1, 0)");
    }
    {
        INFO("C# should read every colour, linear and with its alpha");
        REQUIRE(Text(test.scripting, InstanceListCallsType, "ReadColours", BitsOf(list), 3) ==
                "3: (1, 0, 0, 1) (0, 1, 0, 1) (0, 0, 1, 0.5)");
    }

    test.Store().Replace(list, InstanceListData{{CopyAt(4, 0.0f)}, {}});
    INFO("A list with no colours (every copy white) reads no colours");
    REQUIRE(Text(test.scripting, InstanceListCallsType, "ReadColours", BitsOf(list), 3) == "0:");
}

TEST_CASE("Instance lists from C# with no colours are white, and colours not one per copy are refused",
          "[assets][scripting]")
{
    AssetsScriptingApp test;
    const auto white = HandleFrom<InstanceList>(test.Call<std::uint64_t>(InstanceListCallsType, "AddWhite", 2));
    {
        const InstanceListData* added = test.Store().TryGet(white);
        INFO("The overload with no colours should add two copies and no colours");
        REQUIRE(added != nullptr);
        REQUIRE(added->transforms.size() == 2);
        REQUIRE(added->colours.empty());
    }
    {
        const auto forApp =
            HandleFrom<InstanceList>(test.Call<std::uint64_t>(InstanceListCallsType, "Add", 1, 0, appOwner));
        INFO("A list of one white copy for the app should be added");
        REQUIRE(test.Store().IsValid(forApp));
    }

    {
        INFO("A list of three copies with two colours should give C# the empty handle");
        REQUIRE(test.Call<std::uint64_t>(InstanceListCallsType, "Add", 3, 2, sceneOwner) == 0);
    }
    {
        INFO("Replacing a list with three copies and two colours should fail and leave it as it was");
        REQUIRE_FALSE(test.Call<bool>(InstanceListCallsType, "Replace", BitsOf(white), 3, 0.0f, 2));
        REQUIRE(test.Store().TryGet(white)->transforms.size() == 2);
        REQUIRE(test.Store().Revision(white) == 1);
    }
    INFO("The store should log why it refused each");
    REQUIRE(LoggedWith(test.headless, Testing::LogLevel::Error, "2 colours for 3 transforms") == 2);
}
