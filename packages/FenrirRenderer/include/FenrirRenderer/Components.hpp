#pragma once

#include <cassert>
#include <cstdint>
#include <type_traits>

#include "FenrirAssets/AssetHandle.hpp"
#include "FenrirECS/Entity.hpp"
#include "FenrirInput/InputTypes.hpp"
#include "FenrirMath/Colour.hpp"

namespace Fenrir
{
    /**
     * @brief Draws one mesh with one material at its entity's world transform (ADR-0023): one MeshRenderer, one draw.
     * Plain data, read by the renderer from the active scene every Render; a mesh with several materials is several
     * child entities, each with its own MeshRenderer.
     *
     * An empty or stale mesh is skipped (a stale one logged once per handle). An empty or stale material draws with the
     * fallback material, flat magenta and unlit, logged once per handle. An entity with no Transform draws at the
     * origin.
     */
    struct MeshRenderer
    {
        AssetHandle<Mesh> mesh;
        AssetHandle<Material> material;
        /** @brief False hides this entity only; its children are drawn as their own MeshRenderers say. */
        bool visible = true;
    };

    /**
     * @brief Skins the MeshRenderer beside it from a skeleton's pose (ADR-0035): plain data naming the skeleton entity,
     * the entity carrying FenrirSkeleton's `SkeletonPose`. The mesh is drawn on the GPU from that skeleton's palette.
     *
     * **A skinned item is drawn with the skeleton entity's world matrix. The mesh entity's own Transform and its
     * ancestors are never applied**, unlike an ordinary MeshRenderer's: the mesh's vertices are already in the
     * skeleton's bind space. `visible` and the material stay on the mesh entity's MeshRenderer. SpawnModel makes each
     * skinned mesh entity a child of its skeleton entity at the identity, so the hierarchy reads naturally.
     *
     * A mesh with skin data and no SkinnedMesh is not drawn; nor is one whose skeleton entity is dead, has no
     * `SkeletonPose` or is not `Ready`, whose skeleton it was not baked for (more bones than the skeleton has, or a
     * different bind signature), or when no skinning source is registered (FenrirSkeleton is not installed). A user
     * shader without the `SKINNED` key draws it with the fallback material. Each is logged once as a warning. A
     * SkinnedMesh on a mesh with no skin data is ignored, and the mesh draws rigidly (logged once).
     */
    struct SkinnedMesh
    {
        /** @brief The skeleton entity whose pose skins the mesh. */
        Entity skeleton;
    };

    /**
     * @brief A perspective projection with an infinite far plane and reversed-Z depth (ADR-0026): the near plane maps
     * to depth 1 and the distance to 0. The aspect comes from the viewport's pixel size.
     */
    struct Perspective
    {
        /** @brief The vertical field of view in radians; 60 degrees by default. */
        float verticalFov = 1.04719755f;
        /** @brief The distance to the near plane, in world units. */
        float near = 0.1f;
    };

    /**
     * @brief An orthographic projection with reversed-Z depth (ADR-0026): the near plane maps to depth 1 and the far
     * plane to 0. The width comes from the height and the viewport's pixel size.
     */
    struct Orthographic
    {
        /** @brief The full height of what the view shows, in world units. */
        float height = 10.0f;
        /** @brief The distance to the near plane along the camera's -Z, in world units. */
        float near = 0.1f;
        /** @brief The distance to the far plane along the camera's -Z, in world units. */
        float far = 1000.0f;
    };

    /** @brief Which kind of projection a Projection holds. */
    enum class ProjectionType : std::uint8_t
    {
        Perspective,
        Orthographic,
    };

    /**
     * @brief A camera's projection: exactly one of Perspective and Orthographic, by value, as FenrirPhysics'
     * CollisionShape holds a shape.
     *
     * Assign a kind to change it (`camera.projection = Orthographic{20.0f}`); read or edit it through the checked view
     * (`camera.projection.As<Perspective>().verticalFov = 1.2f`). Reading it as a kind it does not hold asserts.
     */
    class Projection
    {
      public:
        /** @brief The default Perspective. */
        Projection() : Projection(Perspective{})
        {
        }

        /** @brief A perspective projection. Implicit, so one can be assigned where a projection is wanted. */
        Projection(Perspective perspective) : m_type(ProjectionType::Perspective), m_perspective(perspective)
        {
        }

        /** @brief An orthographic projection. Implicit, so one can be assigned where a projection is wanted. */
        Projection(Orthographic orthographic) : m_type(ProjectionType::Orthographic), m_orthographic(orthographic)
        {
        }

        /** @brief The kind held. */
        [[nodiscard]] ProjectionType Type() const
        {
            return m_type;
        }

        /** @brief Whether the projection is a `T` (Perspective or Orthographic). */
        template <typename T>
        [[nodiscard]] bool Is() const
        {
            return m_type == TypeOf<T>();
        }

        /** @brief The projection as a `T`, to read or edit. Asserts that it is one (Is<T>()). */
        template <typename T>
        [[nodiscard]] T& As()
        {
            assert(Is<T>() && "Projection::As: the projection is a different kind");
            return Member<T>(*this);
        }

        /** @copydoc As */
        template <typename T>
        [[nodiscard]] const T& As() const
        {
            assert(Is<T>() && "Projection::As: the projection is a different kind");
            return Member<T>(*this);
        }

      private:
        template <typename T>
        static constexpr ProjectionType TypeOf()
        {
            if constexpr (std::is_same_v<T, Perspective>)
            {
                return ProjectionType::Perspective;
            }
            else
            {
                static_assert(std::is_same_v<T, Orthographic>, "a Projection holds Perspective or Orthographic");
                return ProjectionType::Orthographic;
            }
        }

        template <typename T, typename Self>
        static auto& Member(Self& self)
        {
            if constexpr (TypeOf<T>() == ProjectionType::Perspective)
            {
                return self.m_perspective;
            }
            else
            {
                return self.m_orthographic;
            }
        }

        ProjectionType m_type;
        union {
            Perspective m_perspective;
            Orthographic m_orthographic;
        };
    };

    /** @brief A camera drawing into a window. The foundation renderer draws to the main window only. */
    struct WindowTarget
    {
        /** @brief The main window by default (Window::MainWindow()). A camera naming another window is not drawn. */
        WindowId window{1};
    };

    /**
     * @brief A camera drawing into an image (ADR-0026): an RGBA8 sRGB ImageData with a size and no pixels, made with
     * the asset store's Add and resized with Replace. Materials sample it like any image.
     */
    struct ImageTarget
    {
        AssetHandle<Image> image;
    };

    /** @brief Which kind of render target a RenderTarget holds. */
    enum class RenderTargetType : std::uint8_t
    {
        Window,
        Image,
    };

    /**
     * @brief Where a camera's view goes: exactly one of WindowTarget and ImageTarget, by value, as FenrirPhysics'
     * CollisionShape holds a shape. Assign a kind to change it (`camera.target = ImageTarget{image}`); read it through
     * the checked view (`camera.target.As<ImageTarget>().image`). Reading it as a kind it does not hold asserts.
     */
    class RenderTarget
    {
      public:
        /** @brief The main window. */
        RenderTarget() : RenderTarget(WindowTarget{})
        {
        }

        /** @brief A window. Implicit, so one can be assigned where a render target is wanted. */
        RenderTarget(WindowTarget window) : m_type(RenderTargetType::Window), m_window(window)
        {
        }

        /** @brief An image. Implicit, so one can be assigned where a render target is wanted. */
        RenderTarget(ImageTarget image) : m_type(RenderTargetType::Image), m_image(image)
        {
        }

        /** @brief The kind held. */
        [[nodiscard]] RenderTargetType Type() const
        {
            return m_type;
        }

        /** @brief Whether the target is a `T` (WindowTarget or ImageTarget). */
        template <typename T>
        [[nodiscard]] bool Is() const
        {
            return m_type == TypeOf<T>();
        }

        /** @brief The target as a `T`, to read or edit. Asserts that it is one (Is<T>()). */
        template <typename T>
        [[nodiscard]] T& As()
        {
            assert(Is<T>() && "RenderTarget::As: the target is a different kind");
            return Member<T>(*this);
        }

        /** @copydoc As */
        template <typename T>
        [[nodiscard]] const T& As() const
        {
            assert(Is<T>() && "RenderTarget::As: the target is a different kind");
            return Member<T>(*this);
        }

      private:
        template <typename T>
        static constexpr RenderTargetType TypeOf()
        {
            if constexpr (std::is_same_v<T, WindowTarget>)
            {
                return RenderTargetType::Window;
            }
            else
            {
                static_assert(std::is_same_v<T, ImageTarget>, "a RenderTarget holds WindowTarget or ImageTarget");
                return RenderTargetType::Image;
            }
        }

        template <typename T, typename Self>
        static auto& Member(Self& self)
        {
            if constexpr (TypeOf<T>() == RenderTargetType::Window)
            {
                return self.m_window;
            }
            else
            {
                return self.m_image;
            }
        }

        RenderTargetType m_type;
        union {
            WindowTarget m_window;
            ImageTarget m_image;
        };
    };

    /** @brief A camera background that is the scene's own: its Sky. */
    struct SceneBackground
    {
    };

    /** @brief A camera background of one colour, overriding the scene's. */
    struct ColourBackground
    {
        /** @brief Linear. Black by default (Colour::Black). */
        Colour colour{0.0f, 0.0f, 0.0f, 1.0f};
    };

    /** @brief Which kind of background a CameraBackground holds. */
    enum class BackgroundType : std::uint8_t
    {
        Scene,
        Colour,
    };

    /**
     * @brief What a camera's view shows where nothing was drawn: exactly one of SceneBackground and ColourBackground,
     * by value, as FenrirPhysics' CollisionShape holds a shape. Assign a kind to change it
     * (`camera.background = ColourBackground{Colour::Blue}`); read it through the checked view. Reading it as a kind it
     * does not hold asserts.
     */
    class CameraBackground
    {
      public:
        /** @brief The scene's own background. */
        CameraBackground() : CameraBackground(SceneBackground{})
        {
        }

        /** @brief The scene's own. Implicit, so it can be assigned where a background is wanted. */
        CameraBackground(SceneBackground scene) : m_type(BackgroundType::Scene), m_scene(scene)
        {
        }

        /** @brief One colour. Implicit, so it can be assigned where a background is wanted. */
        CameraBackground(ColourBackground colour) : m_type(BackgroundType::Colour), m_colour(colour)
        {
        }

        /** @brief The kind held. */
        [[nodiscard]] BackgroundType Type() const
        {
            return m_type;
        }

        /** @brief Whether the background is a `T` (SceneBackground or ColourBackground). */
        template <typename T>
        [[nodiscard]] bool Is() const
        {
            return m_type == TypeOf<T>();
        }

        /** @brief The background as a `T`, to read or edit. Asserts that it is one (Is<T>()). */
        template <typename T>
        [[nodiscard]] T& As()
        {
            assert(Is<T>() && "CameraBackground::As: the background is a different kind");
            return Member<T>(*this);
        }

        /** @copydoc As */
        template <typename T>
        [[nodiscard]] const T& As() const
        {
            assert(Is<T>() && "CameraBackground::As: the background is a different kind");
            return Member<T>(*this);
        }

      private:
        template <typename T>
        static constexpr BackgroundType TypeOf()
        {
            if constexpr (std::is_same_v<T, SceneBackground>)
            {
                return BackgroundType::Scene;
            }
            else
            {
                static_assert(std::is_same_v<T, ColourBackground>,
                              "a CameraBackground holds SceneBackground or ColourBackground");
                return BackgroundType::Colour;
            }
        }

        template <typename T, typename Self>
        static auto& Member(Self& self)
        {
            if constexpr (TypeOf<T>() == BackgroundType::Scene)
            {
                return self.m_scene;
            }
            else
            {
                return self.m_colour;
            }
        }

        BackgroundType m_type;
        union {
            SceneBackground m_scene;
            ColourBackground m_colour;
        };
    };

    /**
     * @brief The rectangle of its render target a view draws into, as fractions of the target's width and height from
     * its top-left corner. The whole target by default.
     */
    struct Viewport
    {
        float x = 0.0f;
        float y = 0.0f;
        float width = 1.0f;
        float height = 1.0f;
    };

    /**
     * @brief How a view's Output pass maps its linear HDR colour, after the exposure, into the [0, 1] its render target
     * stores, which the target then encodes as sRGB (ADR-0049).
     */
    enum class Tonemapper : std::uint8_t
    {
        /** @brief Each channel clamped to [0, 1]: what is over 1 is lost. For tests and exact colours. */
        None,
        /**
         * @brief AgX, as Blender and Filament fit it: bright colours desaturate towards white as they would on film,
         * and mid grey shows a little brighter than with None. The default.
         */
        AgX,
        /**
         * @brief Khronos PBR Neutral: colours up to about 0.76 shown as they are (less a small offset in the darks),
         * only the highlights compressed, so a material's base colour reads true.
         */
        PbrNeutral,
        /** @brief ACES, through Stephen Hill's fit of its reference rendering and sRGB output: contrasty, saturated. */
        Aces,
    };

    /**
     * @brief A view's bloom (ADR-0049): the scene blurred over ever wider areas and mixed back in before the tonemap,
     * so bright pixels bleed light into their surroundings. There is no threshold: everything blooms by the same
     * share, and only what is bright shows it. The blur keeps the scene's energy, so bloom moves light, never adds it.
     */
    struct Bloom
    {
        /**
         * @brief The share of the blur the view's colour is mixed with, from 0 to 1: 0.04 by default, a soft glow
         * round what is several times brighter than its surroundings. 0 turns bloom off and costs nothing.
         */
        float intensity = 0.04f;
        /**
         * @brief How far the glow spreads, from 0 (the narrowest blur only, a few pixels) to 1 (the widest only, made
         * at a sixty-fourth of the view's size). 0.7 by default.
         */
        float radius = 0.7f;
    };

    /**
     * @brief Makes a view of the active scene from its entity, looking along the entity's local -Z with +Y up
     * (ADR-0026). Plain data. Every enabled camera in the active scene draws one view a frame, into its viewport of its
     * render target. Views into one target draw by `order`, lowest first, and ties by entity id, logged once. Each view
     * has its own scene targets, and writes only its viewport.
     *
     * Not drawn, logged once: a camera targeting a window other than the main window, a viewport under one pixel, or a
     * target image that is stale, empty or not a render target. A minimised window draws nothing. A window no camera
     * draws into is cleared to black, logged once.
     */
    struct Camera
    {
        Projection projection;
        /** @brief False draws no view. */
        bool enabled = true;
        /** @brief Views into one target draw lowest order first, so a higher order draws over a lower one. */
        std::int32_t order = 0;
        Viewport viewport;
        RenderTarget target;
        CameraBackground background;
        /**
         * @brief Whether the view draws the scene's debug shapes and world text (ADR-0025). Screen text and messages
         * belong to the window, not the view, and draw either way.
         */
        bool drawDebug = true;
        /**
         * @brief The view's exposure in EV stops: its Output pass multiplies the linear colour by 2^exposure before
         * the tonemapper, so each +1 doubles it. 0 by default.
         */
        float exposure = 0.0f;
        /** @brief How the view's Output pass maps its HDR colour into what its target stores (ADR-0049). */
        Tonemapper tonemapper = Tonemapper::AgX;
        /** @brief How the view's bright pixels bleed into their surroundings before the tonemap. */
        Bloom bloom;
    };

    /**
     * @brief The light shining along its entity's world -Z (ADR-0024). The renderer uses the first one in the active
     * scene and warns once if there are more.
     */
    struct DirectionalLight
    {
        /** @brief Linear. White by default (Colour::White). */
        Colour colour{1.0f, 1.0f, 1.0f, 1.0f};
        /** @brief A plain multiplier on the colour. */
        float intensity = 1.0f;
    };

    /**
     * @brief A light shining from its entity's world position in every direction (ADR-0048), any number per scene. Its
     * light falls with the square of the distance, windowed smoothly to zero at `range`: a white light of intensity 1
     * one unit straight above a white rough face lights it about as a DirectionalLight of intensity 1 does, when its
     * range is well past one unit. A light that can light nothing (range or intensity 0, or black) is skipped.
     */
    struct PointLight
    {
        /** @brief Linear. White by default (Colour::White). */
        Colour colour{1.0f, 1.0f, 1.0f, 1.0f};
        /** @brief A plain multiplier on the colour. */
        float intensity = 1.0f;
        /** @brief The distance, in world units, at which the light has fallen to zero; nothing past it is lit. */
        float range = 10.0f;
        /** @brief Whether the light casts shadows. Off by default; not drawn yet. */
        bool castShadows = false;
    };

    /**
     * @brief A light shining from its entity's world position along its world -Z in a cone (ADR-0048), any number per
     * scene. It falls off with distance as a PointLight does, and across its cone smoothly from full inside
     * `innerAngle` to none outside `outerAngle`, both measured from its axis. A light that can light nothing is
     * skipped.
     */
    struct SpotLight
    {
        /** @brief Linear. White by default (Colour::White). */
        Colour colour{1.0f, 1.0f, 1.0f, 1.0f};
        /** @brief A plain multiplier on the colour. */
        float intensity = 1.0f;
        /** @brief The distance, in world units, at which the light has fallen to zero; nothing past it is lit. */
        float range = 10.0f;
        /** @brief The angle off its axis, in radians, inside which the cone is fully lit: 0 by default, as glTF's. */
        float innerAngle = 0.0f;
        /** @brief The angle off its axis, in radians, outside which nothing is lit: 45 degrees by default, as glTF's.
         */
        float outerAngle = 0.785398163f;
        /** @brief Whether the light casts shadows. Off by default; not drawn yet. */
        bool castShadows = false;
    };

    /** @brief A sky of one colour, which lights every surface with that colour from every side (ADR-0047). */
    struct ColourSky
    {
        /** @brief Linear. A dark grey by default, the sky of a scene with none. */
        Colour colour{0.015f, 0.015f, 0.015f, 1.0f};
    };

    /**
     * @brief A sky drawn from a cubemap image (ImageShape::Cubemap, such as one loaded from a `.cubemap` file), sampled
     * by the direction each pixel looks along, so it stays put as the camera moves and turns with it.
     */
    struct CubemapSky
    {
        AssetHandle<Image> cubemap;
        /** @brief A plain multiplier on what the sky draws. */
        float intensity = 1.0f;
        /**
         * @brief How far the sky is turned about world +Y, in radians, counter-clockwise seen from above: a quarter
         * turn brings what was ahead (-Z) round to the left (-X).
         */
        float rotation = 0.0f;
    };

    /**
     * @brief A sky drawn from a panorama (ADR-0047): a flat image twice as wide as it is high wrapping the whole
     * sphere, usually a linear HDR one such as an `.hdr` file. Its centre is ahead (-Z), its right half turns towards
     * +X and its top row is straight up (+Y). The renderer turns it into a cube texture on the GPU once per image
     * revision, then draws it as a CubemapSky.
     */
    struct PanoramaSky
    {
        AssetHandle<Image> image;
        /** @brief A plain multiplier on what the sky draws. */
        float intensity = 1.0f;
        /** @brief How far the sky is turned about world +Y, in radians, as CubemapSky::rotation. */
        float rotation = 0.0f;
    };

    /** @brief Which kind of sky a Sky holds. */
    enum class SkyType : std::uint8_t
    {
        Colour,
        Cubemap,
        Panorama,
    };

    /**
     * @brief What a scene shows where nothing was drawn, and the light every surface takes from all round it, a scene
     * resource (ADR-0026, ADR-0047): exactly one of ColourSky, CubemapSky and PanoramaSky, by value, as FenrirPhysics'
     * CollisionShape holds a shape. A scene without one has the default ColourSky, a dark grey. Each camera whose
     * background is SceneBackground draws it; a ColourBackground replaces only what that camera's view draws behind
     * the scene, and every view is lit by the scene's Sky.
     *
     * The renderer filters a cubemap or panorama sky on the GPU into image-based lighting, once per image revision:
     * its intensity scales the light, and its rotation turns it, without filtering again. A SkyLight lights the scene
     * in its place, leaving it drawn.
     *
     * A cubemap that is empty, stale or not a cubemap with pixels, or a panorama that is empty, stale, or not a flat
     * image with pixels twice as wide as high, draws the default ColourSky instead, logged once. Assign a kind to
     * change it (`sky = PanoramaSky{image}`); read it through the checked view. Reading it as a kind it does not hold
     * asserts.
     */
    class Sky
    {
      public:
        /** @brief The default ColourSky, a dark grey. */
        Sky() : Sky(ColourSky{})
        {
        }

        /** @brief One colour. Implicit, so it can be assigned where a sky is wanted. */
        Sky(ColourSky colour) : m_type(SkyType::Colour), m_colour(colour)
        {
        }

        /** @brief A cubemap. Implicit, so it can be assigned where a sky is wanted. */
        Sky(CubemapSky cubemap) : m_type(SkyType::Cubemap), m_cubemap(cubemap)
        {
        }

        /** @brief A panorama. Implicit, so it can be assigned where a sky is wanted. */
        Sky(PanoramaSky panorama) : m_type(SkyType::Panorama), m_panorama(panorama)
        {
        }

        /** @brief The kind held. */
        [[nodiscard]] SkyType Type() const
        {
            return m_type;
        }

        /** @brief Whether the sky is a `T` (ColourSky, CubemapSky or PanoramaSky). */
        template <typename T>
        [[nodiscard]] bool Is() const
        {
            return m_type == TypeOf<T>();
        }

        /** @brief The sky as a `T`, to read or edit. Asserts that it is one (Is<T>()). */
        template <typename T>
        [[nodiscard]] T& As()
        {
            assert(Is<T>() && "Sky::As: the sky is a different kind");
            return Member<T>(*this);
        }

        /** @copydoc As */
        template <typename T>
        [[nodiscard]] const T& As() const
        {
            assert(Is<T>() && "Sky::As: the sky is a different kind");
            return Member<T>(*this);
        }

      private:
        template <typename T>
        static constexpr SkyType TypeOf()
        {
            if constexpr (std::is_same_v<T, ColourSky>)
            {
                return SkyType::Colour;
            }
            else if constexpr (std::is_same_v<T, CubemapSky>)
            {
                return SkyType::Cubemap;
            }
            else
            {
                static_assert(std::is_same_v<T, PanoramaSky>, "a Sky holds ColourSky, CubemapSky or PanoramaSky");
                return SkyType::Panorama;
            }
        }

        template <typename T, typename Self>
        static auto& Member(Self& self)
        {
            if constexpr (TypeOf<T>() == SkyType::Colour)
            {
                return self.m_colour;
            }
            else if constexpr (TypeOf<T>() == SkyType::Cubemap)
            {
                return self.m_cubemap;
            }
            else
            {
                return self.m_panorama;
            }
        }

        SkyType m_type;
        union {
            ColourSky m_colour;
            CubemapSky m_cubemap;
            PanoramaSky m_panorama;
        };
    };

    /**
     * @brief What lights the scene from all round, when it is not the Sky drawn behind it, a scene resource (ADR-0047):
     * a sky of any kind, used only for its light. A scene without one is lit by its Sky. A skybox chosen for how it
     * looks can light the scene with a neutral ColourSky or a softer panorama instead of its own colours, as a
     * camera's ColourBackground changes what is drawn and not the light. One sky lights the scene either way, so
     * nothing is lit twice.
     *
     * Its sky is read as the Sky is: a cubemap or panorama that cannot be drawn lights with the default ColourSky,
     * logged once, and its intensity and rotation scale and turn the light without filtering it again.
     */
    struct SkyLight
    {
        /** @brief The sky the scene is lit by: the default ColourSky, a dark grey, unless set. */
        Sky sky;
    };
} // namespace Fenrir
