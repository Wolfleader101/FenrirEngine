#include "DeprecatedAmbientLight.hpp"

#include <string_view>

#include "FenrirMath/Colour.hpp"
#include "FenrirRendererScripting/Generated/FenrirRendererMappings.hpp"

namespace Fenrir::Detail
{
    namespace
    {
        /** @brief C#'s deprecated AmbientLight, as Managed/Fenrir.Renderer/AmbientLight.cs lays it out. */
        struct DeprecatedAmbientLight
        {
            Colour colour{0.1f, 0.1f, 0.1f, 1.0f};
            float intensity = 1.0f;
        };
    } // namespace
} // namespace Fenrir::Detail

template <>
struct Iris::StructMapping<Fenrir::Detail::DeprecatedAmbientLight>
{
    static constexpr std::string_view typeName = "Fenrir.Renderer.AmbientLight";
    static constexpr auto fields =
        Iris::StructFields(Iris::Field<Fenrir::Colour>("Colour", 0), Iris::Field<float>("Intensity", 16));
};
static_assert(sizeof(Fenrir::Detail::DeprecatedAmbientLight) == 20,
              "C#'s deprecated AmbientLight is a colour and a float");

namespace Fenrir::Detail
{
    ResourceRegistration DeprecatedAmbientLightResource()
    {
        return MakeSceneResource<DeprecatedAmbientLight>("Fenrir.Renderer.AmbientLight");
    }
} // namespace Fenrir::Detail
