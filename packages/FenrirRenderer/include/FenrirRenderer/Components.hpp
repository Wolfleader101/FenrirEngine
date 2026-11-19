#pragma once

#include <cassert>
#include <cstdint>
#include <type_traits>

#include "FenrirAssets/AssetHandle.hpp"
#include "FenrirECS/Entity.hpp"
#include "FenrirInput/InputTypes.hpp"
#include "FenrirMath/Colour.hpp"
#include "FenrirMath/Math.hpp"

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
        /**
         * @brief Whether it is drawn into shadow maps: the sun's cascades and the shadowed spot and point lights'
         * (ADR-0050). Only an `Opaque` or `Masked` material casts, a Masked one cut out by its alpha cutoff;
         * `AlphaBlend` and `Additive` ones never do.
         */
        bool castShadows = true;
    };

    /**
     * @brief Draws one mesh with one material many times, once per transform of an instance list, with no entity per
     * copy (ADR-0051). Each copy is drawn at the entity's world matrix times its own transform, its colour multiplying
     * the material's base colour in builtin:lit and builtin:unlit. The renderer uploads the list when it is new or its
     * revision has moved on, so a list that never changes uploads once, however far the entity moves.
     *
     * One draw a pass, whatever the count: never merged with other items. Its world bounds are the box around every
     * copy's mesh box (ADR-0057; Renderer::GetWorldBounds), by which a camera it is wholly outside of leaves the whole
     * list out; an opaque or masked list's copies are then culled one by one in the camera's cull pass, and a
     * transparent list is sorted whole by the entity's world position. An
     * empty or stale mesh or list is skipped (a stale one logged once per handle), as is a mesh with skin data; an
     * empty or stale material draws with the fallback material, as a MeshRenderer's does.
     */
    struct InstancedMeshRenderer
    {
        AssetHandle<Mesh> mesh;
        AssetHandle<Material> material;
        AssetHandle<InstanceList> instances;
        /** @brief Whether the copies are drawn into shadow maps, as a MeshRenderer's castShadows says. */
        bool castShadows = true;
        /** @brief False hides every copy; the entity's children are drawn as their own renderers say. */
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
     * @brief A view's anamorphic streaks (ADR-0059): bright pixels drawn out into long, thin horizontal lines, as an
     * anamorphic lens draws them, mixed in before the tonemap after bloom. Like bloom there is no threshold:
     * everything streaks by the same share, and only what is bright shows it. The streaks keep the scene's energy and
     * the tint is taken at unit luminance, so they move light, never add it. Off by default; a view with bloom off
     * still streaks.
     */
    struct AnamorphicStreaks
    {
        /**
         * @brief The share of the streaks the view's colour is mixed with, from 0 to 1. 0 by default, which turns them
         * off and costs nothing; a few hundredths draw long lines from lights and leave dim surfaces alone.
         */
        float intensity = 0.0f;
        /**
         * @brief How long the streaks are, from 0 (the shortest only, a few pixels either side) to 1 (the longest
         * only, reaching most of the way across the view). 0.75 by default.
         */
        float length = 0.75f;
        /**
         * @brief The streaks' colour, linear, scaled to unit luminance: its hue and saturation count, its brightness is
         * ignored. White by default.
         */
        Colour tint{1.0f, 1.0f, 1.0f, 1.0f};
    };

    /**
     * @brief A view's lens flare (ADR-0059), after John Chapman's pseudo lens flare: ghosts of the bright parts of the
     * view, mirrored through its centre along the line from them, and a halo ring, each colour fringed apart, mixed in
     * before the tonemap after the streaks. Like bloom there is no threshold: the flare is the view's own light read
     * from elsewhere, so it moves light and never adds it, and a bright sky ghosts as a faint veiling glare as a real
     * lens does. Off by default.
     *
     * A lens dirt image scales how much of the bloom and the flare each pixel takes where the lens is dirty, so a
     * bright light lights up the smudges on it.
     */
    struct LensFlare
    {
        /**
         * @brief The share of the flare the view's colour is mixed with, from 0 to 1. 0 by default, which turns it off
         * and costs nothing; a few hundredths throw faint ghosts from lights.
         */
        float intensity = 0.0f;
        /** @brief How many ghosts each bright point throws, from 0 to 8: 4 by default. */
        std::uint32_t ghostCount = 4;
        /**
         * @brief How far apart the ghosts are along the line through the view's centre, as a share of the way from
         * each pixel to the centre: 0.4 by default. Each ghost is read that much further along the line than the last,
         * so ghosts past the centre land on the far side of it.
         */
        float ghostSpacing = 0.4f;
        /**
         * @brief The halo ring's radius, as a distance from the view's centre: 0 at the centre, 1 at the corners. 0.6
         * by default. The ring shows what is bright near the centre.
         */
        float haloRadius = 0.6f;
        /** @brief How far the halo reaches either side of its radius, in the same distance: 0.1 by default; 0 none. */
        float haloWidth = 0.1f;
        /**
         * @brief How far red and blue are read apart in each ghost and the halo, along the line through the view's
         * centre, in the same distance: 0.01 by default, so the ghosts fringe.
         */
        float chromaticOffset = 0.01f;
        /**
         * @brief A greyscale image of the dirt on the lens, stretched over the view; none by default. A flat image with
         * pixels; anything else (a cubemap, a render target) has no dirt, logged once.
         */
        AssetHandle<Image> dirt;
        /**
         * @brief How much more of the bloom and the flare a pixel takes where the dirt is white: its share is
         * multiplied by 1 + dirtIntensity times the dirt's brightness, held at or below 1, so the mix stays a blend. 1
         * by default; 0 is no dirt. It needs no flare: dirt with the flare off scales the bloom alone.
         */
        float dirtIntensity = 1.0f;
    };

    /**
     * @brief A view's colour grade (ADR-0059): the look of the picture, set on the exposed scene colour before the
     * tonemapper, in linear Rec. 709. So contrast and saturation act on scene values, mid grey stays put and the
     * tonemapper still rolls off whatever the grade brightens, so a strong grade does not clip. Its steps run in this
     * order: white balance, contrast, split toning, saturation, then the ASC CDL. Identity by default, and identity
     * costs nothing.
     */
    struct ColourGrading
    {
        /**
         * @brief The colour temperature, in kelvin, of the light the view takes as white: 6500 by default (D65, no
         * change). A surface lit by daylight of this temperature comes out grey, so lower cools the picture and higher
         * warms it. Held to 2500 to 25000.
         */
        float temperature = 6500.0f;
        /**
         * @brief How far off daylight that white lies, from -1 to 1: 0 by default. Positive names a greener light, so
         * the picture moves to magenta; negative names a magenta one, so it moves to green.
         */
        float tint = 0.0f;
        /**
         * @brief The slope of the picture in stops around mid grey (0.18), which it keeps: 1 by default, above 1 more
         * contrast, below 1 flatter, 0 all mid grey.
         */
        float contrast = 1.0f;
        /**
         * @brief The tint the shadows take, linear: white by default, and any grey is none. Only its hue and
         * saturation count (it is scaled to unit luminance, so a grey keeps its brightness); the more saturated, the
         * stronger.
         */
        Colour shadowTint{1.0f, 1.0f, 1.0f, 1.0f};
        /** @brief The tint the highlights take, as the shadows' does. */
        Colour highlightTint{1.0f, 1.0f, 1.0f, 1.0f};
        /**
         * @brief Where the shadows end and the highlights start, from -1 to 1: 0 by default, the split at mid grey.
         * Positive moves it down by up to four stops, giving the highlights' tint more of the picture; negative up.
         */
        float balance = 0.0f;
        /**
         * @brief How far each colour is from grey of its luminance: 1 by default, 0 grey, above 1 more saturated. After
         * split toning, so lowering it fades the tints too.
         */
        float saturation = 1.0f;
        /**
         * @brief The ASC CDL, the final trim, per channel: each is multiplied by its slope, then its offset is added,
         * then negatives are clamped and the result raised to its power. The slope, 1 in each channel by default.
         */
        Math::Vec3 slope{1.0f, 1.0f, 1.0f};
        /** @brief The CDL's offset, added after the slope: 0 in each channel by default. */
        Math::Vec3 offset{0.0f, 0.0f, 0.0f};
        /** @brief The CDL's power, held at or above 0.001: 1 in each channel by default. */
        Math::Vec3 power{1.0f, 1.0f, 1.0f};
    };

    /**
     * @brief A view's chromatic aberration (ADR-0059): each colour channel read from its own point as the Output pass
     * reads the scene, red moved outwards and blue inwards, more the further from the centre, as a lens bends each
     * wavelength by its own amount. Edges near the view's corners fringe red outside and blue inside; the centre is as
     * it is. Off by default, and off costs nothing.
     */
    struct ChromaticAberration
    {
        /**
         * @brief How far red and blue move apart at the view's corners, as a fraction of the distance from its centre
         * to a corner: 0 by default, off. A few thousandths fringe the edges; a few hundredths split them.
         */
        float intensity = 0.0f;
    };

    /**
     * @brief A view's vignette (ADR-0059): the exposed colour darkened towards the corners, as a lens's falloff
     * darkens them, before the grade and the tonemap, so the tonemapper sees it. Off by default, and off costs
     * nothing.
     */
    struct Vignette
    {
        /** @brief How much of the light the corners lose, from 0 to 1: 0 by default, off; 1 makes them black. */
        float intensity = 0.0f;
        /**
         * @brief Where the darkening starts, as a distance from the view's centre: 0 at the centre, 1 at the corners.
         * 0.5 by default.
         */
        float radius = 0.5f;
        /**
         * @brief How far past the radius, in the same distance, the darkening takes to reach its intensity: 0.5 by
         * default. 0 is a hard edge.
         */
        float smoothness = 0.5f;
        /**
         * @brief The darkening's shape, from 0 to 1: 1 by default, a circle whatever the view's aspect; 0 an ellipse
         * in the view's own proportions, reaching as far into the long sides as into the short ones.
         */
        float roundness = 1.0f;
    };

    /**
     * @brief A view's film grain (ADR-0059): noise added after the tonemap, in the range the viewer sees, so its
     * strength holds at any exposure. It shows most in the mid-tones and least in deep shadow and bright highlight,
     * as film grain does, and it changes every frame. Off by default, and off costs nothing.
     */
    struct FilmGrain
    {
        /**
         * @brief How far the grain moves a pixel's brightness, at most, as a fraction of the display's range: 0 by
         * default, off. A few hundredths are fine grain.
         */
        float intensity = 0.0f;
        /**
         * @brief How much the grain is kept to the mid-tones, from 0 to 1: 0.8 by default. 0 grains every brightness
         * alike; 1 leaves black and white untouched.
         */
        float response = 0.8f;
        /** @brief The side of each grain, in pixels, at least 1: 1 by default. */
        float size = 1.0f;
    };

    /**
     * @brief How a view turns the scene's light into its picture (ADR-0059): its exposure, the lens's effects, its
     * colour grade, its tonemapper and its film. Plain data, one value: copy it to give another camera the same look.
     * Every effect is off by default, and off costs nothing; the defaults keep the exposure at 0 EV, AgX and a light
     * bloom. In the frame they run in ADR-0059's order: bloom, streaks and the flare in `PostProcess`, then in Output
     * chromatic aberration as the scene is read, the exposure, the vignette, the grade, the tonemapper and the grain.
     */
    struct CameraLook
    {
        /**
         * @brief The view's exposure in EV stops: its Output pass multiplies the linear colour by 2^exposure before
         * the tonemapper, so each +1 doubles it. 0 by default.
         */
        float exposure = 0.0f;
        /** @brief How the view's Output pass maps its HDR colour into what its target stores (ADR-0049). */
        Tonemapper tonemapper = Tonemapper::AgX;
        /** @brief How the view's bright pixels bleed into their surroundings before the tonemap. */
        Bloom bloom;
        /** @brief How the view's bright pixels draw out into horizontal lines before the tonemap, after bloom. */
        AnamorphicStreaks anamorphicStreaks;
        /** @brief The view's ghosts, halo and lens dirt before the tonemap, after the streaks. */
        LensFlare lensFlare;
        /** @brief The view's white balance, contrast, tints and saturation, before the tonemap. */
        ColourGrading colourGrading;
        /** @brief How the view's colours fringe towards its corners, as it reads the scene. */
        ChromaticAberration chromaticAberration;
        /** @brief How the view darkens towards its corners, before the grade. */
        Vignette vignette;
        /** @brief The view's film grain, after its tonemap. */
        FilmGrain filmGrain;
    };

    /**
     * @brief A view's ambient occlusion (ADR-0049): ground-truth ambient occlusion (GTAO, after Intel's XeGTAO) from
     * its depth prepass, darkening the sky's light where nearby geometry hides the sky from a surface, as in creases,
     * corners and under objects resting on the ground. It darkens the sky's diffuse light, and its specular light by a
     * specular occlusion made from it, never the directional, point, spot or rect lights. Transparent items take none.
     */
    struct AmbientOcclusion
    {
        /** @brief Whether the view has it: off by default, and off costs nothing. */
        bool enabled = false;
        /**
         * @brief How far round a surface other surfaces occlude it, in world units (in the camera's own, if its entity
         * is scaled): 0.5 by default. 0 or less turns it off.
         */
        float radius = 0.5f;
        /**
         * @brief How dark the occlusion is: 1 by default, 0 none (and no cost), and above 1 darker. It raises the
         * occlusion to a power, so an unoccluded surface stays unoccluded however strong it is.
         */
        float intensity = 1.0f;
    };

    /**
     * @brief A view's screen-space reflections (ADR-0056): what is on screen mirrored in smooth surfaces, from the
     * previous frame's colour, over the sky's reflection. Each pixel's mirror ray is traced through a hierarchical-Z
     * pyramid of the view's depth, from its vertex normal, and every material lighting through `Fenrir/Lighting.glsl`
     * takes the colour it hit by how sure the hit is and how smooth the material is. They catch only what is on screen:
     * what is off it, or hidden, falls back to the sky's reflection. They show last frame's colour, so a moving
     * object's reflection trails it by a frame.
     */
    struct Reflections
    {
        /**
         * @brief Whether the view has them: off by default, and off costs nothing. On, the view keeps about 44 MB at
         * 1920 x 1080 across frames, and 17 MB more within each frame.
         */
        bool enabled = false;
        /** @brief How much of a sure reflection replaces the sky's: 1 by default, 0 turns them off. */
        float intensity = 1.0f;
        /** @brief How far a mirror ray is traced, in world units: 50 by default. Reflections fade out towards it. */
        float maxDistance = 50.0f;
        /**
         * @brief How far behind a surface, in view depth (world units), a ray may pass and still hit it: 0.5 by
         * default. Thicker catches thin things seen edge on; thinner stops rays catching on what they pass behind.
         */
        float thickness = 0.5f;
        /**
         * @brief The roughness at and above which a surface reflects only the sky: 0.6 by default, fading in from 0.1
         * below it, as a blurred screen is a worse reflection than the sky's own.
         */
        float roughnessCutoff = 0.6f;
    };

    /** @brief What a view shows instead of its lit colour, to look at one of its inputs. */
    enum class CameraDebugView : std::uint8_t
    {
        /** @brief The lit scene, tonemapped: the default. */
        None,
        /**
         * @brief Its ambient occlusion as grey, white unoccluded, with no exposure or tonemap; black where the camera's
         * ambient occlusion is off.
         */
        AmbientOcclusion,
        /**
         * @brief Its lit colour, each pixel tinted by the sun's shadow cascade it reads (ADR-0050): red, green, blue
         * and yellow from the nearest, untinted past the last or without shadows.
         */
        ShadowCascades,
        /**
         * @brief The sun's shadow atlas for this view as grey, its cascades side by side from the nearest, white
         * nearest the sun, with no exposure or tonemap; black where nothing casts.
         */
        ShadowAtlas,
        /**
         * @brief The frame's spot shadow atlas as grey (ADR-0050), each shadowed spot light's tile along the rows from
         * the top left, nearest the view first, white nearest its light, with no exposure or tonemap; black where no
         * spot light casts.
         */
        SpotShadowAtlas,
        /**
         * @brief Each lit pixel as the heat of how many lights it loops over (ADR-0049), its cluster's or, unclustered,
         * every light: black for none, then blue at 1, cyan at 4, green at 16, yellow at 64 and red at 256, the most a
         * cluster lists, on a log scale, exposed and tonemapped.
         */
        LightClusters,
        /**
         * @brief Its screen-space reflections (ADR-0056): at each pixel, the colour its mirror ray hit, from last
         * frame, times how sure the hit is, exposed, vignetted, graded, tonemapped and grained; black where nothing is
         * reflected or the camera's reflections are off. It is what a mirror there would blend, sharp: a material's
         * roughness blurs and fades it.
         */
        Reflections,
        /**
         * @brief How sure each pixel's reflection hit is, as grey from black (no hit) to white, with no exposure or
         * tonemap; black where the camera's reflections are off.
         */
        ReflectionConfidence,
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
        /** @brief How the view turns the scene's light into its picture: exposure, lens, grade, tonemap and film. */
        CameraLook look;
        /** @brief How nearby geometry darkens the sky's light in the view. */
        AmbientOcclusion ambientOcclusion;
        /** @brief How smooth surfaces in the view mirror what is on screen. */
        Reflections reflections;
        /** @brief What the view shows: its lit colour, or one of its inputs. */
        CameraDebugView debugView = CameraDebugView::None;
    };

    /**
     * @brief The light shining along its entity's world -Z (ADR-0024). The renderer uses the first one in the active
     * scene and warns once if there are more.
     *
     * It casts shadows unless `castShadows` is off (ADR-0050): each camera gets cascaded shadow maps out to
     * `shadowDistance`, up to RendererConfig::shadowCascades of them, split by RendererConfig::shadowSplitLambda,
     * blended across their boundaries and fading out at the last. Each cascade is snapped to whole texels of its map,
     * so its shadows hold still as the camera moves and turns. Edges are filtered by 16 rotated taps of hardware
     * depth comparison. The biases trade shadow acne (speckled self-shadowing on lit faces) against peter-panning
     * (shadows coming loose from their casters): raise them for acne, lower them for gaps.
     */
    struct DirectionalLight
    {
        /** @brief Linear. White by default (Colour::White). */
        Colour colour{1.0f, 1.0f, 1.0f, 1.0f};
        /** @brief A plain multiplier on the colour. */
        float intensity = 1.0f;
        /** @brief Whether it casts shadows: on by default. */
        bool castShadows = true;
        /**
         * @brief How far from each camera, along its view, its shadows reach, in world units: 100 by default. Past
         * it, everything is lit. The nearer it is, the sharper the shadows within it.
         */
        float shadowDistance = 100.0f;
        /**
         * @brief How far a lit point moves out along its normal before its shadow is looked up, in shadow-map texels
         * (each cascade's own size): 1 by default.
         */
        float shadowNormalBias = 1.0f;
        /**
         * @brief How far it moves towards the light, in texels per unit of slope (the tangent of the angle between its
         * normal and the light, held between 0.1 and 8): 2 by default.
         */
        float shadowSlopeBias = 2.0f;
    };

    /**
     * @brief A light shining from its entity's world position in every direction (ADR-0048), any number per scene. Its
     * light falls with the square of the distance, windowed smoothly to zero at `range`: a white light of intensity 1
     * one unit straight above a white rough face lights it about as a DirectionalLight of intensity 1 does, when its
     * range is well past one unit. A light that can light nothing (range or intensity 0, or black) is skipped.
     *
     * With `castShadows` on it casts shadows (ADR-0050) while it is within RendererConfig::pointShadowBudget: each
     * frame the shadowed point lights nearest to any camera keep them, each drawn into a cube of one shared cube array,
     * as far as its range, and the rest light unshadowed, warned about once. Edges are filtered and biased as the
     * DirectionalLight's are. A mesh round the light's position, such as a glowing bulb, shadows everything: turn that
     * mesh's castShadows off.
     */
    struct PointLight
    {
        /** @brief Linear. White by default (Colour::White). */
        Colour colour{1.0f, 1.0f, 1.0f, 1.0f};
        /** @brief A plain multiplier on the colour. */
        float intensity = 1.0f;
        /** @brief The distance, in world units, at which the light has fallen to zero; nothing past it is lit. */
        float range = 10.0f;
        /**
         * @brief Whether the light casts shadows: off by default. Only RendererConfig::pointShadowBudget of them do in
         * a frame, the nearest to the cameras.
         */
        bool castShadows = false;
        /**
         * @brief How far a lit point moves out along its normal before its shadow is looked up, in shadow-map texels
         * (whose size grows with the distance from the light): 1 by default.
         */
        float shadowNormalBias = 1.0f;
        /**
         * @brief How far it moves towards the light, in texels per unit of slope (the tangent of the angle between its
         * normal and the light, held between 0.1 and 8): 2 by default.
         */
        float shadowSlopeBias = 2.0f;
    };

    /**
     * @brief A light shining from its entity's world position along its world -Z in a cone (ADR-0048), any number per
     * scene. It falls off with distance as a PointLight does, and across its cone smoothly from full inside
     * `innerAngle` to none outside `outerAngle`, both measured from its axis. A light that can light nothing is
     * skipped.
     *
     * With `castShadows` on it casts shadows (ADR-0050) while it is within RendererConfig::spotShadowBudget: each
     * frame the shadowed spot lights nearest to any camera keep them, each drawn into a tile of one shared atlas
     * through a perspective along its cone (held to 80 degrees off its axis), and the rest light unshadowed, warned
     * about once. Edges are filtered and biased as the DirectionalLight's are. A mesh round the light's position, such
     * as a glowing bulb, shadows all of its cone: turn that mesh's castShadows off.
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
        /**
         * @brief Whether the light casts shadows: off by default. Only RendererConfig::spotShadowBudget of them do in
         * a frame, the nearest to the cameras.
         */
        bool castShadows = false;
        /**
         * @brief How far a lit point moves out along its normal before its shadow is looked up, in shadow-map texels
         * (whose size grows with the distance from the light): 1 by default.
         */
        float shadowNormalBias = 1.0f;
        /**
         * @brief How far it moves towards the light, in texels per unit of slope (the tangent of the angle between its
         * normal and the light, held between 0.1 and 8): 2 by default.
         */
        float shadowSlopeBias = 2.0f;
    };

    /**
     * @brief A glowing rectangle centred on its entity, `width` along its world +X and `height` along its world +Y,
     * shining from its face towards its world -Z (and from its back too when `twoSided`), any number per scene
     * (ADR-0048). Its highlights take its shape, blurring with the surface's roughness: it is shaded with linearly
     * transformed cosines, and casts no shadows. Its intensity is its radiance: a white rect light of intensity 1
     * filling the view of a white rough face lights it about 1, and one subtending less lights it less, as the sky
     * does. Its light is windowed smoothly to zero at `range` from the rectangle. The rectangle itself is not drawn:
     * give it an emissive mesh to see it. A light that can light nothing (range, intensity, width or height 0, or
     * black) is skipped.
     */
    struct RectLight
    {
        /** @brief Linear. White by default (Colour::White). */
        Colour colour{1.0f, 1.0f, 1.0f, 1.0f};
        /** @brief A plain multiplier on the colour. */
        float intensity = 1.0f;
        /** @brief Its size along its entity's world +X, in world units; the entity's scale does not stretch it. */
        float width = 1.0f;
        /** @brief Its size along its entity's world +Y, in world units. */
        float height = 1.0f;
        /** @brief Whether it shines from its back face as well as its front. */
        bool twoSided = false;
        /** @brief The distance from the rectangle, in world units, at which its light has fallen to zero. */
        float range = 10.0f;
        /**
         * @brief Whether the scene draws the light's surface, an emissive mesh where it is: true by default. Then, in a
         * camera with reflections, its specular yields to the reflection of that surface where it is on screen, so it
         * is not reflected twice. Set it false for a light with nothing drawn, which keeps its specular everywhere.
         */
        bool visibleSurface = true;
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
