// Drawing from C# (spec seam 7): a C# system's MeshRenderer and Camera drawn on the null backend, the Sky C# writes
// reaching the frame, and C#'s deprecated AmbientLight doing nothing, all read back through RendererProbe.

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "FenrirECS/DefaultComponents.hpp"
#include "RendererScriptingApp.hpp"

using namespace Fenrir;
using namespace Fenrir::Testing;

namespace
{
    bool Near(float a, float b)
    {
        return std::abs(a - b) < 1e-6f;
    }

    // The first draw of the last frame's Opaque pass, after making sure there is exactly one.
    RecordedDraw OnlyOpaqueDraw(const RendererScriptingApp& test)
    {
        const std::vector<RecordedDraw> draws = DrawsIn(test.Submitted(), "Opaque");
        INFO("The frame should draw exactly the one MeshRenderer");
        REQUIRE(draws.size() == 1);
        return draws[0];
    }

    // The first Background pass's binding group at set 2, which holds a colour sky's block.
    std::vector<float> BackgroundColour(const RendererScriptingApp& test)
    {
        using namespace Fenrir::Graphics;
        BindingGroupHandle group;
        bool inBackground = false;
        for (const CommandList& list : test.Submitted())
        {
            for (const Command& command : list.commands)
            {
                if (const auto* begin = std::get_if<CmdBeginPass>(&command))
                {
                    inBackground = begin->pass.label == "Background";
                }
                else if (const auto* set = std::get_if<CmdSetBindingGroup>(&command);
                         set != nullptr && inBackground && set->set == 2 && group.IsEmpty())
                {
                    group = set->group;
                }
            }
        }
        std::size_t offset = 0;
        const std::vector<std::byte> bytes = BoundBytes(test.Device(), group, 0, &offset);
        INFO("The Background pass should bind a colour block");
        REQUIRE(!bytes.empty());
        return {FloatAt(bytes, offset), FloatAt(bytes, offset + 4), FloatAt(bytes, offset + 8)};
    }
} // namespace

TEST_CASE("A C# system loads a mesh, makes a material, sets its colour, and spawns a MeshRenderer and a Camera that "
          "are drawn",
          "[renderer][scripting]")
{
    RendererScriptingApp test;
    test.Call<void>(SceneBuilderType, "Arm");
    // The first frame runs the system, whose entities play back after it; the Render phase draws them.
    test.Frame();

    const auto drawn = test.Call<std::uint32_t>(SceneBuilderType, "DrawnEntity");
    const auto camera = test.Call<std::uint32_t>(SceneBuilderType, "CameraEntity");
    {
        INFO("The C# system's entities should be alive with their MeshRenderer and Camera");
        REQUIRE(test.Entities().HasEntity(drawn));
        REQUIRE(test.Entities().GetEntity(drawn).HasComponent<MeshRenderer>());
        REQUIRE(test.Entities().GetEntity(camera).HasComponent<Camera>());
    }
    const RecordedDraw draw = OnlyOpaqueDraw(test);
    {
        // Column 3 of the world matrix in the draw's record (set 3, binding 0; 128 bytes a record).
        std::size_t offset = 0;
        const std::vector<std::byte> records = BoundBytes(test.Device(), draw.groups.at(3), 0, &offset);
        const std::size_t at = offset + std::size_t{draw.draw.firstInstance} * 128 + 48;
        INFO("The draw should be at (1, 2, 3), where C# placed the entity");
        REQUIRE(Near(FloatAt(records, at), 1.0f));
        REQUIRE(Near(FloatAt(records, at + 4), 2.0f));
        REQUIRE(Near(FloatAt(records, at + 8), 3.0f));
    }
    std::size_t offset = 0;
    const std::vector<std::byte> block = BoundBytes(test.Device(), draw.groups.at(2), 0, &offset);
    INFO("The material block should hold the baseColour C# set, (0.25, 0.5, 0.75, 1), at its start");
    REQUIRE(Near(FloatAt(block, offset), 0.25f));
    REQUIRE(Near(FloatAt(block, offset + 4), 0.5f));
    REQUIRE(Near(FloatAt(block, offset + 8), 0.75f));
    REQUIRE(Near(FloatAt(block, offset + 12), 1.0f));
}

TEST_CASE("A Sky written from C# is what the Background pass draws, and C++'s sky reads back in C#",
          "[renderer][scripting]")
{
    RendererScriptingApp test;
    test.Call<void>(SceneBuilderType, "Arm");
    test.Call<void>(ResourceCallsType, "SkyColour", 0.2f, 0.4f, 0.6f);
    test.ApplyChanges();
    test.Frame();
    {
        const std::vector<float> rgb = BackgroundColour(test);
        INFO("The Background pass should fill with the colour C# inserted, (0.2, 0.4, 0.6)");
        REQUIRE((Near(rgb[0], 0.2f) && Near(rgb[1], 0.4f) && Near(rgb[2], 0.6f)));
    }

    // Written in place the second time, as the scene now has one.
    test.Call<void>(ResourceCallsType, "SkyColour", 0.7f, 0.1f, 0.3f);
    test.Frame();
    {
        const std::vector<float> rgb = BackgroundColour(test);
        INFO("The Background pass should fill with the colour C# wrote in place, (0.7, 0.1, 0.3)");
        REQUIRE((Near(rgb[0], 0.7f) && Near(rgb[1], 0.1f) && Near(rgb[2], 0.3f)));
    }

    *test.Get().GetActiveScene().GetResource<Sky>() = CubemapSky{AssetHandle<Image>{7, 9}};
    INFO("C# should read the cubemap sky C++ wrote, naming its image (7, 9)");
    REQUIRE(test.Call<int>(ResourceCallsType, "ReadSky") == 1);
    REQUIRE(Exactly(test.Call<float>(ResourceCallsType, "Value", 0), 7.0f));
    REQUIRE(Exactly(test.Call<float>(ResourceCallsType, "Value", 1), 9.0f));
}

TEST_CASE("C#'s deprecated AmbientLight can still be inserted and written, and lights nothing", "[renderer][scripting]")
{
    RendererScriptingApp test;
    test.Call<void>(SceneBuilderType, "Arm");
    test.Frame();
    const std::vector<Fenrir::Graphics::BindingGroupEntry> before =
        Fenrir::RendererProbe::Entries(test.Device(), OnlyOpaqueDraw(test).groups.at(0));

    INFO("inserting one, then writing it, from C# runs");
    test.Call<void>(ResourceCallsType, "Ambient", 0.5f, 0.25f, 1.0f, 2.0f);
    test.ApplyChanges();
    test.Call<void>(ResourceCallsType, "Ambient", 1.0f, 1.0f, 1.0f, 4.0f);
    test.ApplyChanges();
    test.Frame();

    INFO("the frame set binds the same sky light as before: the default ColourSky's");
    const std::vector<Fenrir::Graphics::BindingGroupEntry> after =
        Fenrir::RendererProbe::Entries(test.Device(), OnlyOpaqueDraw(test).groups.at(0));
    REQUIRE(after.size() == 5);
    REQUIRE(after[1].texture == before[1].texture);
    REQUIRE(after[2].texture == before[2].texture);
}
