#pragma once

// How FenrirRenderer's components, resources and values look to C#: each mapping names its mirror in the
// Fenrir.Renderer contract (Managed/Fenrir.Renderer), and is checked against it when a script assembly loads, so a
// drift fails the load and names the field. The extension's tests check the values the native functions pass through
// pointers with the same mappings.

#include <Iris/Iris.hpp>

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "FenrirAssetsScripting/AssetMappings.hpp"
#include "FenrirInputScripting/Generated/FenrirInputMappings.hpp"
#include "FenrirRenderer/Components.hpp"
#include "FenrirRenderer/Renderer.hpp"
#include "FenrirScripting/Generated/FenrirScriptingMappings.hpp"

namespace Fenrir::Detail
{
    // Projection, RenderTarget, CameraBackground and Sky keep their tag and union private, so these stand in for them,
    // and for Camera, which holds three: the same members in the same order (a one-byte tag, then the union at its own
    // alignment), nameable by a mapping, as FenrirPhysics's CollisionShape is mapped. MakeComponent and
    // MakeSceneResource check the size and alignment, and the asserts below every member's offset.
    struct ProjectionLayout
    {
        ProjectionType type = ProjectionType::Perspective;
        union {
            Perspective perspective{};
            Orthographic orthographic;
        };
    };

    struct RenderTargetLayout
    {
        RenderTargetType type = RenderTargetType::Window;
        union {
            WindowTarget window{};
            ImageTarget image;
        };
    };

    // SceneBackground is empty, which Iris cannot map: the one byte it takes stands in for it, as for FenrirPhysics's
    // FixedJoint.
    struct SceneBackgroundLayout
    {
        std::uint8_t unused = 0;
    };

    struct CameraBackgroundLayout
    {
        BackgroundType type = BackgroundType::Scene;
        union {
            SceneBackgroundLayout scene{};
            ColourBackground colour;
        };
    };

    struct CameraLayout
    {
        ProjectionLayout projection;
        bool enabled = true;
        std::int32_t order = 0;
        Viewport viewport;
        RenderTargetLayout target;
        CameraBackgroundLayout background;
        bool drawDebug = true;
    };

    struct SkyLayout
    {
        SkyType type = SkyType::Colour;
        union {
            ColourSky colour{};
            CubemapSky cubemap;
        };
    };

    static_assert(sizeof(SceneBackgroundLayout) == sizeof(SceneBackground) &&
                      alignof(SceneBackgroundLayout) == alignof(SceneBackground),
                  "SceneBackground changed shape: update SceneBackgroundLayout and the C# SceneBackground.");
    static_assert(sizeof(ProjectionLayout) == sizeof(Projection) && alignof(ProjectionLayout) == alignof(Projection),
                  "Projection changed shape: update ProjectionLayout and the C# CameraProjection.");
    static_assert(sizeof(RenderTargetLayout) == sizeof(RenderTarget) &&
                      alignof(RenderTargetLayout) == alignof(RenderTarget),
                  "RenderTarget changed shape: update RenderTargetLayout and the C# RenderTarget.");
    static_assert(sizeof(CameraBackgroundLayout) == sizeof(CameraBackground) &&
                      alignof(CameraBackgroundLayout) == alignof(CameraBackground),
                  "CameraBackground changed shape: update CameraBackgroundLayout and the C# CameraBackground.");
    static_assert(sizeof(SkyLayout) == sizeof(Sky) && alignof(SkyLayout) == alignof(Sky),
                  "Sky changed shape: update SkyLayout and the C# Sky.");
    static_assert(sizeof(CameraLayout) == sizeof(Camera) && alignof(CameraLayout) == alignof(Camera),
                  "Camera changed shape: update CameraLayout and the C# Camera.");
    static_assert(offsetof(CameraLayout, enabled) == offsetof(Camera, enabled) &&
                      offsetof(CameraLayout, order) == offsetof(Camera, order) &&
                      offsetof(CameraLayout, viewport) == offsetof(Camera, viewport) &&
                      offsetof(CameraLayout, target) == offsetof(Camera, target) &&
                      offsetof(CameraLayout, background) == offsetof(Camera, background) &&
                      offsetof(CameraLayout, drawDebug) == offsetof(Camera, drawDebug),
                  "Camera's members moved: update CameraLayout and the C# Camera.");
} // namespace Fenrir::Detail

template <>
struct Iris::StructMapping<Fenrir::MeshRenderer>
{
    static constexpr std::string_view typeName = "Fenrir.Renderer.MeshRenderer";
    static constexpr auto fields = Iris::StructFields(IRIS_FIELD_AS(Fenrir::MeshRenderer, mesh, "Mesh"),
                                                      IRIS_FIELD_AS(Fenrir::MeshRenderer, material, "Material"),
                                                      IRIS_FIELD_AS(Fenrir::MeshRenderer, visible, "Visible"));
};

template <>
struct Iris::EnumMapping<Fenrir::ProjectionType>
{
    static constexpr std::string_view typeName = "Fenrir.Renderer.ProjectionType";
};

template <>
struct Iris::EnumMapping<Fenrir::RenderTargetType>
{
    static constexpr std::string_view typeName = "Fenrir.Renderer.RenderTargetType";
};

template <>
struct Iris::EnumMapping<Fenrir::BackgroundType>
{
    static constexpr std::string_view typeName = "Fenrir.Renderer.BackgroundType";
};

template <>
struct Iris::EnumMapping<Fenrir::SkyType>
{
    static constexpr std::string_view typeName = "Fenrir.Renderer.SkyType";
};

template <>
struct Iris::StructMapping<Fenrir::Perspective>
{
    static constexpr std::string_view typeName = "Fenrir.Renderer.Perspective";
    static constexpr auto fields = Iris::StructFields(IRIS_FIELD_AS(Fenrir::Perspective, verticalFov, "VerticalFov"),
                                                      IRIS_FIELD_AS(Fenrir::Perspective, near, "Near"));
};

template <>
struct Iris::StructMapping<Fenrir::Orthographic>
{
    static constexpr std::string_view typeName = "Fenrir.Renderer.Orthographic";
    static constexpr auto fields = Iris::StructFields(IRIS_FIELD_AS(Fenrir::Orthographic, height, "Height"),
                                                      IRIS_FIELD_AS(Fenrir::Orthographic, near, "Near"),
                                                      IRIS_FIELD_AS(Fenrir::Orthographic, far, "Far"));
};

// The tag and every kind, in declaration order, each kind at the union's offset (the C# explicit layout mirrors it).
template <>
struct Iris::StructMapping<Fenrir::Detail::ProjectionLayout>
{
    using Layout = Fenrir::Detail::ProjectionLayout;
    static constexpr std::string_view typeName = "Fenrir.Renderer.CameraProjection";
    static constexpr auto fields =
        Iris::StructFields(IRIS_FIELD_AS(Layout, type, "_type"), IRIS_FIELD_AS(Layout, perspective, "_perspective"),
                           IRIS_FIELD_AS(Layout, orthographic, "_orthographic"));
};

template <>
struct Iris::StructMapping<Fenrir::WindowTarget>
{
    static constexpr std::string_view typeName = "Fenrir.Renderer.WindowTarget";
    static constexpr auto fields = Iris::StructFields(IRIS_FIELD_AS(Fenrir::WindowTarget, window, "Window"));
};

template <>
struct Iris::StructMapping<Fenrir::ImageTarget>
{
    static constexpr std::string_view typeName = "Fenrir.Renderer.ImageTarget";
    static constexpr auto fields = Iris::StructFields(IRIS_FIELD_AS(Fenrir::ImageTarget, image, "Image"));
};

template <>
struct Iris::StructMapping<Fenrir::Detail::RenderTargetLayout>
{
    using Layout = Fenrir::Detail::RenderTargetLayout;
    static constexpr std::string_view typeName = "Fenrir.Renderer.RenderTarget";
    static constexpr auto fields =
        Iris::StructFields(IRIS_FIELD_AS(Layout, type, "_type"), IRIS_FIELD_AS(Layout, window, "_window"),
                           IRIS_FIELD_AS(Layout, image, "_image"));
};

template <>
struct Iris::StructMapping<Fenrir::Detail::SceneBackgroundLayout>
{
    static constexpr std::string_view typeName = "Fenrir.Renderer.SceneBackground";
    static constexpr auto fields =
        Iris::StructFields(IRIS_FIELD_AS(Fenrir::Detail::SceneBackgroundLayout, unused, "_unused"));
};

template <>
struct Iris::StructMapping<Fenrir::ColourBackground>
{
    static constexpr std::string_view typeName = "Fenrir.Renderer.ColourBackground";
    static constexpr auto fields = Iris::StructFields(IRIS_FIELD_AS(Fenrir::ColourBackground, colour, "Colour"));
};

template <>
struct Iris::StructMapping<Fenrir::Detail::CameraBackgroundLayout>
{
    using Layout = Fenrir::Detail::CameraBackgroundLayout;
    static constexpr std::string_view typeName = "Fenrir.Renderer.CameraBackground";
    static constexpr auto fields =
        Iris::StructFields(IRIS_FIELD_AS(Layout, type, "_type"), IRIS_FIELD_AS(Layout, scene, "_scene"),
                           IRIS_FIELD_AS(Layout, colour, "_colour"));
};

template <>
struct Iris::StructMapping<Fenrir::Viewport>
{
    static constexpr std::string_view typeName = "Fenrir.Renderer.Viewport";
    static constexpr auto fields = Iris::StructFields(
        IRIS_FIELD_AS(Fenrir::Viewport, x, "X"), IRIS_FIELD_AS(Fenrir::Viewport, y, "Y"),
        IRIS_FIELD_AS(Fenrir::Viewport, width, "Width"), IRIS_FIELD_AS(Fenrir::Viewport, height, "Height"));
};

template <>
struct Iris::StructMapping<Fenrir::Detail::CameraLayout>
{
    using Layout = Fenrir::Detail::CameraLayout;
    static constexpr std::string_view typeName = "Fenrir.Renderer.Camera";
    static constexpr auto fields =
        Iris::StructFields(IRIS_FIELD_AS(Layout, projection, "Projection"), IRIS_FIELD_AS(Layout, enabled, "Enabled"),
                           IRIS_FIELD_AS(Layout, order, "Order"), IRIS_FIELD_AS(Layout, viewport, "Viewport"),
                           IRIS_FIELD_AS(Layout, target, "Target"), IRIS_FIELD_AS(Layout, background, "Background"),
                           IRIS_FIELD_AS(Layout, drawDebug, "DrawDebug"));
};

template <>
struct Iris::StructMapping<Fenrir::DirectionalLight>
{
    static constexpr std::string_view typeName = "Fenrir.Renderer.DirectionalLight";
    static constexpr auto fields = Iris::StructFields(IRIS_FIELD_AS(Fenrir::DirectionalLight, colour, "Colour"),
                                                      IRIS_FIELD_AS(Fenrir::DirectionalLight, intensity, "Intensity"));
};

template <>
struct Iris::StructMapping<Fenrir::AmbientLight>
{
    static constexpr std::string_view typeName = "Fenrir.Renderer.AmbientLight";
    static constexpr auto fields = Iris::StructFields(IRIS_FIELD_AS(Fenrir::AmbientLight, colour, "Colour"),
                                                      IRIS_FIELD_AS(Fenrir::AmbientLight, intensity, "Intensity"));
};

template <>
struct Iris::StructMapping<Fenrir::ColourSky>
{
    static constexpr std::string_view typeName = "Fenrir.Renderer.ColourSky";
    static constexpr auto fields = Iris::StructFields(IRIS_FIELD_AS(Fenrir::ColourSky, colour, "Colour"));
};

template <>
struct Iris::StructMapping<Fenrir::CubemapSky>
{
    static constexpr std::string_view typeName = "Fenrir.Renderer.CubemapSky";
    static constexpr auto fields = Iris::StructFields(IRIS_FIELD_AS(Fenrir::CubemapSky, cubemap, "Cubemap"));
};

template <>
struct Iris::StructMapping<Fenrir::Detail::SkyLayout>
{
    using Layout = Fenrir::Detail::SkyLayout;
    static constexpr std::string_view typeName = "Fenrir.Renderer.Sky";
    static constexpr auto fields =
        Iris::StructFields(IRIS_FIELD_AS(Layout, type, "_type"), IRIS_FIELD_AS(Layout, colour, "_colour"),
                           IRIS_FIELD_AS(Layout, cubemap, "_cubemap"));
};

template <>
struct Iris::StructMapping<Fenrir::RendererStats>
{
    static constexpr std::string_view typeName = "Fenrir.Renderer.RendererStats";
    static constexpr auto fields = Iris::StructFields(IRIS_FIELD_AS(Fenrir::RendererStats, draws, "Draws"),
                                                      IRIS_FIELD_AS(Fenrir::RendererStats, passes, "Passes"),
                                                      IRIS_FIELD_AS(Fenrir::RendererStats, uploads, "Uploads"));
};
