#pragma once

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "Core/BlockRing.hpp"
#include "Core/PunctualLights.hpp"
#include "Core/RenderGraph.hpp"
#include "Device/GraphicsDevice.hpp"
#include "FenrirLogger/ILogger.hpp"
#include "FenrirMath/Math.hpp"

namespace Fenrir::Render
{
    // What spot and point shadows share (ADR-0050): each kind casts within a budget, the lights nearest to the views
    // keeping their shadows each frame, drawn into maps pooled by the budget.

    /** @brief How one kind of light's shadow maps are made, from RendererConfig: its spot or point shadow members. */
    struct ShadowBudgetConfig
    {
        /** @brief How many lights of the kind may cast shadows in a frame; 0 shadows none. */
        std::uint32_t budget = 0;
        /** @brief The side of each light's map, in texels: a spot light's atlas tile, or a point light's cube face. */
        std::uint32_t mapSize = 0;
    };

    /** @brief Which spot or point lights cast this frame: their places in the frame's light list, nearest first. */
    struct ShadowChoice
    {
        /** @brief At most the budget of them. */
        std::vector<std::size_t> lights;
        /** @brief How many lights of the kind asked to cast: more than `lights` when the budget left some out. */
        std::size_t wanted = 0;
    };

    /**
     * @brief The lights of `kind` in `lights` with castShadows that keep their shadows: the `budget` nearest to any of
     * the cameras at `cameras` (the distance from each light's position to the nearest camera), the nearer first, ties
     * in list order. With no camera, the first `budget` in list order.
     */
    [[nodiscard]] ShadowChoice ChooseShadowedLights(std::span<const PunctualLight> lights,
                                                    std::span<const Math::Vec3> cameras, std::uint32_t budget,
                                                    LightKind kind);

    /**
     * @brief What a ShadowPool needs of its kind of light, as SpotShadowKind and PointShadowKind give it: the `Light`
     * it chooses from, its `Name` in the warning, the least `MinMapSize` of a map, the labels of its map (`MapLabel`),
     * the map in the graph (`MapResource`) and its records (`RecordsLabel`), its `Shadow` (what its passes draw with)
     * by `Fit`, its `Record` by `ToRecord`, and its map's description by `MapDesc`. A kind may also have
     * `MakeFallback(device)`, to make and own the map bound when none of its lights casts.
     */
    template <typename Kind>
    concept ShadowKind = requires(const PunctualLight& light, const ShadowBudgetConfig& config,
                                  const typename Kind::Shadow& shadow, std::uint32_t index) {
        typename Kind::Record;
        { Kind::Light } -> std::convertible_to<LightKind>;
        { Kind::Name } -> std::convertible_to<const char*>;
        { Kind::MinMapSize } -> std::convertible_to<std::uint32_t>;
        { Kind::MapLabel } -> std::convertible_to<const char*>;
        { Kind::MapResource } -> std::convertible_to<const char*>;
        { Kind::RecordsLabel } -> std::convertible_to<const char*>;
        { Kind::Fit(light, config) } -> std::same_as<typename Kind::Shadow>;
        { Kind::ToRecord(light, shadow, index, config) } -> std::same_as<typename Kind::Record>;
        { Kind::MapDesc(config) } -> std::same_as<Graphics::TextureDesc>;
    };

    /**
     * @brief One kind of light's shadow pool (ADR-0050): its budget, each frame's choice of the lights that keep their
     * shadows, warned about once when the budget leaves some out, their records in a ring set 0 binds, and the maps
     * they are drawn into, kept across frames and made again when the budget or size changes. Drawing the casters
     * into the maps is the caller's. A kind without MakeFallback is given the map bound when none of its lights casts.
     */
    template <ShadowKind Kind>
    class ShadowPool
    {
      public:
        using Shadow = typename Kind::Shadow;
        using Record = typename Kind::Record;

        /** @brief What one frame's shadows of the kind are, from Prepare; valid until the next Prepare. */
        struct Frame
        {
            /** @brief The lights that keep their shadows, as places in the frame's light list, in their maps' order. */
            std::span<const std::size_t> lights;
            /** @brief Each one's shadow, in the same order. */
            std::span<const Shadow> shadows;
            /** @brief The maps, imported into the frame's graph; none when no light of the kind casts. */
            GraphResource map;
        };

        /**
         * @brief An empty pool of `config`'s budget, its map size held to at least the kind's least. `fallback` is the
         * map bound when no light casts, kept by the caller, for a kind without MakeFallback.
         */
        ShadowPool(Graphics::GraphicsDevice& device, ILogger& logger, ShadowBudgetConfig config,
                   Graphics::TextureHandle fallback = {})
            : m_device(device), m_logger(logger), m_config(config), m_fallback(fallback)
        {
            m_config.mapSize = std::max(m_config.mapSize, Kind::MinMapSize);
            if constexpr (requires { Kind::MakeFallback(device); })
            {
                m_fallback = Kind::MakeFallback(device);
                m_ownsFallback = true;
            }
            m_records = std::make_unique<BlockRing>(m_device, Graphics::BindingGroupLayoutHandle{}, true,
                                                    sizeof(Record), Kind::RecordsLabel);
            m_records->Reserve(1);
        }
        ~ShadowPool()
        {
            m_records.reset();
            m_device.Destroy(m_map);
            if (m_ownsFallback)
            {
                m_device.Destroy(m_fallback);
            }
        }
        ShadowPool(const ShadowPool&) = delete;
        ShadowPool& operator=(const ShadowPool&) = delete;
        ShadowPool(ShadowPool&&) = delete;
        ShadowPool& operator=(ShadowPool&&) = delete;

        /** @brief The budget and map size. */
        [[nodiscard]] const ShadowBudgetConfig& Config() const noexcept
        {
            return m_config;
        }

        /**
         * @brief How many lights may cast from the next frame; the maps go when it changes, made again at the new size
         * when a light next casts, and not kept meanwhile.
         */
        void SetBudget(std::uint32_t budget)
        {
            if (budget == m_config.budget)
            {
                return;
            }
            m_config.budget = budget;
            m_device.Destroy(m_map);
            m_map = {};
        }

        /**
         * @brief The lights of the kind in `lights` that keep their shadows, by ChooseShadowedLights from `cameras`;
         * warns once, the first time the budget leaves some out. Prepare takes it once the frame knows what casts.
         */
        [[nodiscard]] ShadowChoice Choose(std::span<const PunctualLight> lights, std::span<const Math::Vec3> cameras)
        {
            ShadowChoice choice = ChooseShadowedLights(lights, cameras, m_config.budget, Kind::Light);
            if (choice.wanted > choice.lights.size() && !m_loggedBudget)
            {
                m_loggedBudget = true;
                m_logger.Warn("FenrirRenderer: {} {} lights cast shadows, past the {} shadow budget of {}, so those "
                              "farthest from the cameras light unshadowed",
                              choice.wanted, Kind::Name, Kind::Name, m_config.budget);
            }
            return choice;
        }

        /**
         * @brief Frame `frameIndex`'s shadows of the kind from `choice` (none when nothing `casts`): each light's
         * shadow fitted, its record written, and the maps made if they are missing or the wrong size and imported into
         * `graph`. `lights` is the light list `choice` was made from.
         */
        [[nodiscard]] Frame Prepare(ShadowChoice choice, std::uint64_t frameIndex, RenderGraph& graph,
                                    std::span<const PunctualLight> lights, bool casts)
        {
            m_lights = casts ? std::move(choice.lights) : std::vector<std::size_t>{};
            m_shadows.clear();
            m_records->Reserve(std::max<std::size_t>(m_lights.size(), 1));
            for (std::size_t i = 0; i < m_lights.size(); ++i)
            {
                const PunctualLight& light = lights[m_lights[i]];
                m_shadows.push_back(Kind::Fit(light, m_config));
                const Record record = Kind::ToRecord(light, m_shadows.back(), static_cast<std::uint32_t>(i), m_config);
                m_records->Write(frameIndex, i, std::as_bytes(std::span(&record, 1)));
            }
            Frame frame{m_lights, m_shadows, {}};
            if (!m_lights.empty())
            {
                const Graphics::TextureDesc desc = Kind::MapDesc(m_config);
                if (m_map.IsEmpty() || m_mapDesc != desc)
                {
                    m_device.Destroy(m_map);
                    m_map = m_device.CreateTexture(desc, Kind::MapLabel);
                    m_mapDesc = desc;
                }
                frame.map = graph.ImportTexture(Kind::MapResource, m_map, desc);
            }
            return frame;
        }

        /** @brief The maps this frame's lights are drawn into; empty when none casts. */
        [[nodiscard]] Graphics::TextureHandle Map() const noexcept
        {
            return m_lights.empty() ? Graphics::TextureHandle{} : m_map;
        }

        /** @brief The map that lights everything, bound in place of the maps where they are not read. */
        [[nodiscard]] Graphics::TextureHandle Fallback() const noexcept
        {
            return m_fallback;
        }

        /** @brief What set 0 binds for the kind this frame: the maps, or the fallback when none casts. */
        [[nodiscard]] Graphics::TextureHandle Bound() const noexcept
        {
            return m_lights.empty() ? m_fallback : m_map;
        }

        /** @brief Frame `frameIndex`'s copy of the records, which set 0 binds. */
        [[nodiscard]] Graphics::BufferHandle Records(std::uint64_t frameIndex) const
        {
            return m_records->Buffer(frameIndex);
        }

      private:
        Graphics::GraphicsDevice& m_device;
        ILogger& m_logger;
        /** @brief The budget and map size, the size held to the kind's least. */
        ShadowBudgetConfig m_config;
        /** @brief The map that lights everything: the kind's own when it has MakeFallback, else the caller's. */
        Graphics::TextureHandle m_fallback;
        /** @brief Whether the pool made `m_fallback`, and so destroys it. */
        bool m_ownsFallback = false;
        /** @brief This frame's records, one a shadowed light, in the order of `m_lights`. */
        std::unique_ptr<BlockRing> m_records;
        /** @brief The maps, made when a light first casts and again when the budget or size changes; else empty. */
        Graphics::TextureHandle m_map;
        /** @brief What `m_map` was made with. */
        Graphics::TextureDesc m_mapDesc;
        /** @brief Whether the budget has left lights out yet, already logged. */
        bool m_loggedBudget = false;
        /** @brief This frame's shadowed lights, as places in its light list, in their maps' order. */
        std::vector<std::size_t> m_lights;
        /** @brief Each one's shadow, in the same order. */
        std::vector<Shadow> m_shadows;
    };
} // namespace Fenrir::Render
