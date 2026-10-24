#pragma once

#include <cassert>
#include <cstdint>
#include <type_traits>

#include "FenrirAssets/AssetHandle.hpp"
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
        /** @brief Linear. */
        Colour colour = Colour::Black;
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
    };

    /**
     * @brief The light shining along its entity's world -Z (ADR-0024). The renderer uses the first one in the active
     * scene and warns once if there are more.
     */
    struct DirectionalLight
    {
        /** @brief Linear. */
        Colour colour = Colour::White;
        /** @brief A plain multiplier on the colour. */
        float intensity = 1.0f;
    };

    /**
     * @brief The constant light every lit surface receives, a scene resource (ADR-0024). A scene without one is lit by
     * a default AmbientLight, a dim grey.
     */
    struct AmbientLight
    {
        /** @brief Linear. */
        Colour colour{0.1f, 0.1f, 0.1f, 1.0f};
        /** @brief A plain multiplier on the colour. */
        float intensity = 1.0f;
    };

    /** @brief A sky of one colour. */
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
    };

    /** @brief Which kind of sky a Sky holds. */
    enum class SkyType : std::uint8_t
    {
        Colour,
        Cubemap,
    };

    /**
     * @brief What a scene shows where nothing was drawn, a scene resource beside AmbientLight (ADR-0026): exactly one
     * of ColourSky and CubemapSky, by value, as FenrirPhysics' CollisionShape holds a shape. A scene without one has
     * the default ColourSky, a dark grey. Each camera whose background is SceneBackground draws it; a ColourBackground
     * overrides it for that camera's view.
     *
     * A cubemap that is empty, stale or not a cubemap with pixels draws the default ColourSky instead, logged once.
     * Assign a kind to change it (`sky = CubemapSky{cubemap}`); read it through the checked view. Reading it as a kind
     * it does not hold asserts.
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

        /** @brief The kind held. */
        [[nodiscard]] SkyType Type() const
        {
            return m_type;
        }

        /** @brief Whether the sky is a `T` (ColourSky or CubemapSky). */
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
            else
            {
                static_assert(std::is_same_v<T, CubemapSky>, "a Sky holds ColourSky or CubemapSky");
                return SkyType::Cubemap;
            }
        }

        template <typename T, typename Self>
        static auto& Member(Self& self)
        {
            if constexpr (TypeOf<T>() == SkyType::Colour)
            {
                return self.m_colour;
            }
            else
            {
                return self.m_cubemap;
            }
        }

        SkyType m_type;
        union {
            ColourSky m_colour;
            CubemapSky m_cubemap;
        };
    };
} // namespace Fenrir
