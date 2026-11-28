#include "DemoRoundTrip.hpp"

#include <format>

namespace FenrirDemo::Detail
{
    SceneRoundTripCounts SceneRoundTripCounts::Of(const Fenrir::SceneSaveResult& save)
    {
        return {save.entitiesWritten, save.componentsWritten};
    }

    std::optional<std::string> RoundTripMismatch(std::string_view scene, const SceneRoundTripCounts& saved,
                                                 const SceneRoundTripCounts& reloaded)
    {
        if (saved.entities == reloaded.entities && saved.components == reloaded.components)
        {
            return std::nullopt;
        }
        return std::format("FENRIR_DEMO_SMOKE: the scene \"{}\" did not load back whole from its scene file: {} "
                           "entities and {} components saved, {} entities and {} components loaded back",
                           scene, saved.entities, saved.components, reloaded.entities, reloaded.components);
    }
} // namespace FenrirDemo::Detail
