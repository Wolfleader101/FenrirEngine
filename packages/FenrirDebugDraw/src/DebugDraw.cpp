#include "FenrirDebugDraw/DebugDraw.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>

#include "DebugDrawState.hpp"
#include "FenrirApp/App.hpp"
#include "FenrirECS/WorldTransform.hpp"
#include "FenrirScheduler/Scheduler.hpp"
#include "Shapes.hpp"

namespace Fenrir
{
    namespace Detail
    {
        Lifetime LifetimeFor(float duration, const App& app)
        {
            const Time& time = app.GetTime();
            if (std::isinf(duration) && duration > 0.0f)
            {
                return {Lifetime::Kind::Forever, time.frameCount, 0.0};
            }
            if (duration > 0.0f)
            {
                return {Lifetime::Kind::Seconds, time.frameCount, static_cast<double>(duration)};
            }
            const std::optional<SchedulePriority> phase = app.CurrentPhase();
            const bool ticking = phase == SchedulePriority::PreTick || phase == SchedulePriority::Tick ||
                                 phase == SchedulePriority::PostTick;
            if (ticking)
            {
                return {Lifetime::Kind::UntilNextTick, time.tickCount, 0.0};
            }
            return {Lifetime::Kind::ThisFrame, time.frameCount, 0.0};
        }

        bool LifetimeIsLive(const Lifetime& lifetime, const App& app)
        {
            if (lifetime.kind == Lifetime::Kind::ThisFrame)
            {
                return app.GetTime().frameCount == lifetime.stamp;
            }
            if (lifetime.kind == Lifetime::Kind::UntilNextTick)
            {
                return app.GetTime().tickCount == lifetime.stamp;
            }
            if (lifetime.kind == Lifetime::Kind::Seconds)
            {
                // A little slack, so time counted down in steps that do not add up exactly still ends on time.
                return lifetime.seconds > 1e-9;
            }
            return true; // Forever
        }
    } // namespace Detail

    namespace
    {
        enum class ItemKind : std::uint8_t
        {
            Lines,
            Triangles,
            ScreenText,
            Message,
            WorldText,
        };

        // One debug draw item, its positions in its relativeTo entity's space when it has one.
        struct Item
        {
            Detail::Lifetime lifetime;
            std::size_t group = 0;
            ItemKind kind = ItemKind::Lines;
            bool relative = false;
            Entity relativeTo;
            DepthMode depth = DepthMode::Tested;
            float thickness = 1.0f;
            std::vector<DebugVertex> vertices;
            std::string text;
            Math::Vec3 position = Math::Vec3(0.0f);
            float size = 16.0f;
            Colour colour;
            std::optional<std::int64_t> key;
        };

        Math::Vec3 Place(const Math::Mat4& transform, const Math::Vec3& local)
        {
            return Math::Vec3(transform * Math::Vec4(local, 1.0f));
        }

        int Segments(const ShapeOptions& options)
        {
            return std::max(3, options.segments);
        }
    } // namespace

    struct DebugDraw::Records
    {
        struct Group
        {
            std::string name;
            bool shown = true;
        };

        std::vector<Group> groups;
        std::vector<Item> items;

        // The group's index, made shown if it is new.
        std::size_t GroupIndex(std::string_view name)
        {
            for (std::size_t i = 0; i < groups.size(); ++i)
            {
                if (groups[i].name == name)
                {
                    return i;
                }
            }
            groups.push_back({std::string(name), true});
            return groups.size() - 1;
        }

        [[nodiscard]] const Group* FindGroup(std::string_view name) const
        {
            for (const Group& group : groups)
            {
                if (group.name == name)
                {
                    return &group;
                }
            }
            return nullptr;
        }

        // Records `item` in the named group, unless its relativeTo entity is already gone.
        void Add(Item item, std::string_view groupName)
        {
            if (item.relative && !item.relativeTo.IsValid())
            {
                return;
            }
            item.group = GroupIndex(groupName);
            if (item.key.has_value())
            {
                for (Item& existing : items)
                {
                    if (existing.kind == ItemKind::Message && existing.key == item.key)
                    {
                        existing = std::move(item);
                        return;
                    }
                }
            }
            items.push_back(std::move(item));
        }
    };

    DebugDraw::DebugDraw(const Detail::DebugDrawState& state) : m_state(&state), m_records(std::make_unique<Records>())
    {
    }

    DebugDraw::~DebugDraw() = default;

    void DebugDraw::Record(std::function<void(Records&)> change)
    {
        if (SendBuffer* buffer = SendBuffer::OfThisThread())
        {
            buffer->Add([this, held = std::move(change)] { held(*m_records); });
            return;
        }
        change(*m_records);
    }

    namespace
    {
        // An item stamped with its lifetime now, from the options every call takes.
        template <typename Options>
        Item MakeItem(ItemKind kind, const Options& options, const App& app)
        {
            Item item;
            item.kind = kind;
            item.lifetime = Detail::LifetimeFor(options.duration, app);
            item.relative = options.relativeTo != Entity();
            item.relativeTo = options.relativeTo;
            return item;
        }

        Item ShapeItem(ItemKind kind, const Detail::Shapes::Points& points, const Colour& colour,
                       const ShapeOptions& options, const App& app)
        {
            Item item = MakeItem(kind, options, app);
            item.depth = options.depth;
            item.thickness = options.thickness;
            item.vertices.reserve(points.size());
            for (const Math::Vec3& point : points)
            {
                item.vertices.push_back({point, colour});
            }
            return item;
        }
    } // namespace

    namespace
    {
        // The change that records `item` in `group`, for DebugDraw::Record.
        auto Keeping(Item item, std::string_view group)
        {
            return
                [recorded = std::move(item), name = std::string(group)](auto& records) { records.Add(recorded, name); };
        }
    } // namespace

    namespace
    {
        // The shape calls all end the same way: make the points, then record them as one item.
        template <typename Make>
        Item Lines(const Colour& colour, const ShapeOptions& options, const App& app, Make&& make)
        {
            Detail::Shapes::Points points;
            make(points);
            return ShapeItem(ItemKind::Lines, points, colour, options, app);
        }

        template <typename Make>
        Item Solid(const Colour& colour, const ShapeOptions& options, const App& app, Make&& make)
        {
            Detail::Shapes::Points points;
            make(points);
            return ShapeItem(ItemKind::Triangles, points, colour, options, app);
        }
    } // namespace

    void DebugDraw::Line(const Math::Vec3& from, const Math::Vec3& to, const Colour& colour,
                         const ShapeOptions& options)
    {
        Item item = Lines(colour, options, *m_state->app, [&](Detail::Shapes::Points& out) {
            out.push_back(from);
            out.push_back(to);
        });
        Record(Keeping(std::move(item), options.group));
    }

    void DebugDraw::Ray(const Math::Vec3& origin, const Math::Vec3& direction, const Colour& colour,
                        const ShapeOptions& options)
    {
        Line(origin, origin + direction, colour, options);
    }

    void DebugDraw::Arrow(const Math::Vec3& from, const Math::Vec3& to, float headLength, const Colour& colour,
                          const ShapeOptions& options)
    {
        Item item = Lines(colour, options, *m_state->app,
                          [&](Detail::Shapes::Points& out) { Detail::Shapes::Arrow(out, from, to, headLength); });
        Record(Keeping(std::move(item), options.group));
    }

    void DebugDraw::Polyline(std::span<const Math::Vec3> points, bool closed, const Colour& colour,
                             const ShapeOptions& options)
    {
        Item item = Lines(colour, options, *m_state->app,
                          [&](Detail::Shapes::Points& out) { Detail::Shapes::Polyline(out, points, closed); });
        Record(Keeping(std::move(item), options.group));
    }

    void DebugDraw::Point(const Math::Vec3& position, float pixelSize, const Colour& colour,
                          const ShapeOptions& options)
    {
        Item item = Lines(colour, options, *m_state->app, [&](Detail::Shapes::Points& out) {
            out.push_back(position);
            out.push_back(position);
        });
        item.thickness = pixelSize;
        Record(Keeping(std::move(item), options.group));
    }

    void DebugDraw::Box(const Math::AABB& box, const Colour& colour, const ShapeOptions& options)
    {
        Box((box.min + box.max) * 0.5f, (box.max - box.min) * 0.5f, Math::Quat(1.0f, 0.0f, 0.0f, 0.0f), colour,
            options);
    }

    void DebugDraw::Box(const Math::Vec3& centre, const Math::Vec3& halfExtents, const Math::Quat& rotation,
                        const Colour& colour, const ShapeOptions& options)
    {
        Item item = Lines(colour, options, *m_state->app, [&](Detail::Shapes::Points& out) {
            Detail::Shapes::WireBox(out, centre, halfExtents, rotation);
        });
        Record(Keeping(std::move(item), options.group));
    }

    void DebugDraw::Sphere(const Math::Vec3& centre, float radius, const Colour& colour, const ShapeOptions& options)
    {
        Item item = Lines(colour, options, *m_state->app, [&](Detail::Shapes::Points& out) {
            Detail::Shapes::WireSphere(out, centre, radius, Segments(options));
        });
        Record(Keeping(std::move(item), options.group));
    }

    void DebugDraw::Capsule(const Math::Vec3& from, const Math::Vec3& to, float radius, const Colour& colour,
                            const ShapeOptions& options)
    {
        Item item = Lines(colour, options, *m_state->app, [&](Detail::Shapes::Points& out) {
            Detail::Shapes::WireCapsule(out, from, to, radius, Segments(options));
        });
        Record(Keeping(std::move(item), options.group));
    }

    void DebugDraw::Cylinder(const Math::Vec3& from, const Math::Vec3& to, float radius, const Colour& colour,
                             const ShapeOptions& options)
    {
        Item item = Lines(colour, options, *m_state->app, [&](Detail::Shapes::Points& out) {
            Detail::Shapes::WireCylinder(out, from, to, radius, Segments(options));
        });
        Record(Keeping(std::move(item), options.group));
    }

    void DebugDraw::Cone(const Math::Vec3& apex, const Math::Vec3& baseCentre, float radius, const Colour& colour,
                         const ShapeOptions& options)
    {
        Item item = Lines(colour, options, *m_state->app, [&](Detail::Shapes::Points& out) {
            Detail::Shapes::WireCone(out, apex, baseCentre, radius, Segments(options));
        });
        Record(Keeping(std::move(item), options.group));
    }

    void DebugDraw::Circle(const Math::Vec3& centre, const Math::Vec3& normal, float radius, const Colour& colour,
                           const ShapeOptions& options)
    {
        Item item = Lines(colour, options, *m_state->app, [&](Detail::Shapes::Points& out) {
            Detail::Shapes::Circle(out, centre, normal, radius, Segments(options));
        });
        Record(Keeping(std::move(item), options.group));
    }

    void DebugDraw::Arc(const Math::Vec3& centre, const Math::Vec3& normal, const Math::Vec3& start, float angle,
                        const Colour& colour, const ShapeOptions& options)
    {
        constexpr float fullTurn = 6.28318530717958647692f;
        const int segments = std::max(
            1, static_cast<int>(std::ceil(static_cast<float>(Segments(options)) * std::abs(angle) / fullTurn)));
        Item item = Lines(colour, options, *m_state->app, [&](Detail::Shapes::Points& out) {
            Detail::Shapes::Arc(out, centre, normal, start, angle, segments);
        });
        Record(Keeping(std::move(item), options.group));
    }

    void DebugDraw::Plane(const Math::Vec3& point, const Math::Vec3& normal, float halfSize, const Colour& colour,
                          const ShapeOptions& options)
    {
        Item item = Lines(colour, options, *m_state->app,
                          [&](Detail::Shapes::Points& out) { Detail::Shapes::Plane(out, point, normal, halfSize); });
        Record(Keeping(std::move(item), options.group));
    }

    void DebugDraw::Grid(const Math::Vec3& centre, const Math::Quat& rotation, int cells, float cellSize,
                         const Colour& colour, const ShapeOptions& options)
    {
        Item item = Lines(colour, options, *m_state->app, [&](Detail::Shapes::Points& out) {
            Detail::Shapes::Grid(out, centre, rotation, std::max(1, cells), cellSize);
        });
        Record(Keeping(std::move(item), options.group));
    }

    void DebugDraw::Axes(const Math::Mat4& transform, float length, const ShapeOptions& options)
    {
        Item item = MakeItem(ItemKind::Lines, options, *m_state->app);
        item.depth = options.depth;
        item.thickness = options.thickness;
        const Math::Vec3 origin(transform[3]);
        const std::array<Colour, 3> colours = {Colour::Red, Colour::Green, Colour::Blue};
        for (int axis = 0; axis < 3; ++axis)
        {
            const Colour& colour = colours[static_cast<std::size_t>(axis)];
            item.vertices.push_back({origin, colour});
            item.vertices.push_back({origin + Math::Vec3(transform[axis]) * length, colour});
        }
        Record(Keeping(std::move(item), options.group));
    }

    void DebugDraw::Frustum(const Math::Mat4& viewProjection, const Colour& colour, const ShapeOptions& options,
                            float farDepth)
    {
        Item item = Lines(colour, options, *m_state->app,
                          [&](Detail::Shapes::Points& out) { Detail::Shapes::Frustum(out, viewProjection, farDepth); });
        Record(Keeping(std::move(item), options.group));
    }

    void DebugDraw::SolidBox(const Math::AABB& box, const Colour& colour, const ShapeOptions& options)
    {
        SolidBox((box.min + box.max) * 0.5f, (box.max - box.min) * 0.5f, Math::Quat(1.0f, 0.0f, 0.0f, 0.0f), colour,
                 options);
    }

    void DebugDraw::SolidBox(const Math::Vec3& centre, const Math::Vec3& halfExtents, const Math::Quat& rotation,
                             const Colour& colour, const ShapeOptions& options)
    {
        Item item = Solid(colour, options, *m_state->app, [&](Detail::Shapes::Points& out) {
            Detail::Shapes::SolidBox(out, centre, halfExtents, rotation);
        });
        Record(Keeping(std::move(item), options.group));
    }

    void DebugDraw::SolidSphere(const Math::Vec3& centre, float radius, const Colour& colour,
                                const ShapeOptions& options)
    {
        Item item = Solid(colour, options, *m_state->app, [&](Detail::Shapes::Points& out) {
            Detail::Shapes::SolidSphere(out, centre, radius, Segments(options));
        });
        Record(Keeping(std::move(item), options.group));
    }

    void DebugDraw::SolidCapsule(const Math::Vec3& from, const Math::Vec3& to, float radius, const Colour& colour,
                                 const ShapeOptions& options)
    {
        Item item = Solid(colour, options, *m_state->app, [&](Detail::Shapes::Points& out) {
            Detail::Shapes::SolidCapsule(out, from, to, radius, Segments(options));
        });
        Record(Keeping(std::move(item), options.group));
    }

    void DebugDraw::Triangles(std::span<const Math::Vec3> vertices, const Colour& colour, const ShapeOptions& options)
    {
        Item item = Solid(colour, options, *m_state->app, [&](Detail::Shapes::Points& out) {
            out.assign(vertices.begin(), vertices.begin() + static_cast<std::ptrdiff_t>(vertices.size() / 3 * 3));
        });
        Record(Keeping(std::move(item), options.group));
    }

    namespace
    {
        Item TextItem(ItemKind kind, const Math::Vec3& position, std::string_view text, const Colour& colour,
                      const TextOptions& options, const App& app)
        {
            Item item = MakeItem(kind, options, app);
            item.text = std::string(text);
            item.position = position;
            item.size = options.size;
            item.colour = colour;
            return item;
        }
    } // namespace

    void DebugDraw::ScreenText(const Math::Vec2& position, std::string_view text, const Colour& colour,
                               const TextOptions& options)
    {
        Item item = TextItem(ItemKind::ScreenText, Math::Vec3(position, 0.0f), text, colour, options, *m_state->app);
        Record(Keeping(std::move(item), options.group));
    }

    void DebugDraw::Message(std::string_view text, const Colour& colour, const TextOptions& options)
    {
        Item item = TextItem(ItemKind::Message, Math::Vec3(0.0f), text, colour, options, *m_state->app);
        Record(Keeping(std::move(item), options.group));
    }

    void DebugDraw::Message(std::int64_t key, std::string_view text, const Colour& colour, const TextOptions& options)
    {
        Item item = TextItem(ItemKind::Message, Math::Vec3(0.0f), text, colour, options, *m_state->app);
        item.key = key;
        Record(Keeping(std::move(item), options.group));
    }

    void DebugDraw::WorldText(const Math::Vec3& position, std::string_view text, const Colour& colour,
                              const TextOptions& options)
    {
        Item item = TextItem(ItemKind::WorldText, position, text, colour, options, *m_state->app);
        Record(Keeping(std::move(item), options.group));
    }

    void DebugDraw::ShowGroup(std::string_view group)
    {
        Record(
            [name = std::string(group)](Records& records) { records.groups[records.GroupIndex(name)].shown = true; });
    }

    void DebugDraw::HideGroup(std::string_view group)
    {
        Record(
            [name = std::string(group)](Records& records) { records.groups[records.GroupIndex(name)].shown = false; });
    }

    bool DebugDraw::IsGroupShown(std::string_view group) const
    {
        const Records::Group* found = m_records->FindGroup(group);
        return found == nullptr || found->shown;
    }

    void DebugDraw::Clear()
    {
        Record([](Records& records) { records.items.clear(); });
    }

    void DebugDraw::Clear(std::string_view group)
    {
        Record([name = std::string(group)](Records& records) {
            const std::size_t index = records.GroupIndex(name);
            std::erase_if(records.items, [index](const Item& item) { return item.group == index; });
        });
    }

    namespace
    {
        bool IsLive(const Item& item, const App& app)
        {
            return Detail::LifetimeIsLive(item.lifetime, app) && (!item.relative || item.relativeTo.IsValid());
        }
    } // namespace

    DebugBatches DebugDraw::Tessellate() const
    {
        const App& app = *m_state->app;
        DebugBatches batches;
        std::vector<DebugTextRun> messages;
        std::vector<DebugTextRun> worldTexts;
        for (const Item& item : m_records->items)
        {
            if (!IsLive(item, app) || !m_records->groups[item.group].shown)
            {
                continue;
            }
            const Math::Mat4 transform = item.relative ? GetWorldMatrix(item.relativeTo) : Math::Mat4(1.0f);
            if (item.kind == ItemKind::Lines)
            {
                auto batch = std::find_if(batches.lines.begin(), batches.lines.end(), [&item](const DebugLineBatch& b) {
                    return b.depth == item.depth && std::equal_to<float>()(b.thickness, item.thickness);
                });
                if (batch == batches.lines.end())
                {
                    batch = batches.lines.insert(batches.lines.end(), DebugLineBatch{item.depth, item.thickness, {}});
                }
                for (const DebugVertex& vertex : item.vertices)
                {
                    batch->vertices.push_back({Place(transform, vertex.position), vertex.colour});
                }
            }
            else if (item.kind == ItemKind::Triangles)
            {
                auto batch = std::find_if(batches.triangles.begin(), batches.triangles.end(),
                                          [&item](const DebugTriangleBatch& b) { return b.depth == item.depth; });
                if (batch == batches.triangles.end())
                {
                    batch = batches.triangles.insert(batches.triangles.end(), DebugTriangleBatch{item.depth, {}});
                }
                for (const DebugVertex& vertex : item.vertices)
                {
                    batch->vertices.push_back({Place(transform, vertex.position), vertex.colour});
                }
            }
            else if (item.kind == ItemKind::ScreenText)
            {
                batches.texts.push_back({DebugTextSpace::Screen, item.text, item.position, item.size, item.colour});
            }
            else if (item.kind == ItemKind::Message)
            {
                messages.push_back({DebugTextSpace::Message, item.text, item.position, item.size, item.colour});
            }
            else
            {
                worldTexts.push_back(
                    {DebugTextSpace::World, item.text, Place(transform, item.position), item.size, item.colour});
            }
        }
        batches.texts.insert(batches.texts.end(), messages.rbegin(), messages.rend());
        batches.texts.insert(batches.texts.end(), worldTexts.begin(), worldTexts.end());
        return batches;
    }

    void DebugDraw::Expire(double deltaTime)
    {
        const App& app = *m_state->app;
        const std::uint64_t frame = app.GetTime().frameCount;
        for (Item& item : m_records->items)
        {
            // An item drawn this frame, before this system ran, has not lived through any of this frame's time.
            if (item.lifetime.kind == Detail::Lifetime::Kind::Seconds && item.lifetime.stamp < frame)
            {
                item.lifetime.seconds -= deltaTime;
            }
        }
        std::erase_if(m_records->items, [&app](const Item& item) { return !IsLive(item, app); });
    }
} // namespace Fenrir
