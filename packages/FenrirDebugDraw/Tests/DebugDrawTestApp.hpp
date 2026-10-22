#pragma once

// What FenrirDebugDraw's tests share: an App with debug drawing installed, driven one frame at a time, that keeps the
// active scene's tessellated batches as its Render phase sees them.

#include <cmath>
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include "FenrirApp/App.hpp"
#include "FenrirDebugDraw/DebugDraw.hpp"
#include "FenrirDebugDraw/DebugDrawing.hpp"

namespace DebugDrawTest
{
    // A logger that drops everything: a second ConsoleLogger in one process throws.
    class SilentLogger : public Fenrir::ILogger
    {
      public:
        void AddLogger(std::shared_ptr<ILogger>) override
        {
        }
        void RemoveLogger(std::shared_ptr<ILogger>) override
        {
        }

      protected:
        void LogImpl(std::string_view) override
        {
        }
        void InfoImpl(std::string_view) override
        {
        }
        void WarnImpl(std::string_view) override
        {
        }
        void ErrorImpl(std::string_view) override
        {
        }
        void FatalImpl(std::string_view) override
        {
        }
    };

    inline constexpr double TickRate = 1.0 / 60.0;

    // An App with debug drawing installed. Each frame's Render phase tessellates the active scene's DebugDraw into
    // `seen`, after every Render system the test adds before running.
    class TestApp
    {
      public:
        TestApp() : app(std::make_unique<SilentLogger>(), 4), drawing(Fenrir::DebugDrawing::Install(app))
        {
        }

        // Adds the Render system that records what is drawn. Call it after adding the test's own systems.
        void WatchRender()
        {
            app.AddSequentialSystem(Fenrir::SchedulePriority::Render, [this](Fenrir::App& running) {
                seen = running.GetActiveScene().GetResource<Fenrir::DebugDraw>()->Tessellate();
            });
        }

        // Runs one frame of `deltaTime` seconds and returns what its Render phase saw.
        const Fenrir::DebugBatches& Frame(double deltaTime)
        {
            seen = {};
            app.RunFrame(deltaTime);
            return seen;
        }

        // A frame with no tick: shorter than the tick rate, with nothing owed.
        const Fenrir::DebugBatches& FrameWithNoTick()
        {
            return Frame(TickRate * 0.25);
        }

        Fenrir::DebugDraw& Draw()
        {
            return *app.GetActiveScene().GetResource<Fenrir::DebugDraw>();
        }

        Fenrir::App app;
        Fenrir::DebugDrawing drawing;
        Fenrir::DebugBatches seen;
    };

    // One line of a tessellated batch.
    struct Segment
    {
        Fenrir::Math::Vec3 a;
        Fenrir::Math::Vec3 b;
    };

    inline bool Near(const Fenrir::Math::Vec3& x, const Fenrir::Math::Vec3& y, float tolerance = 1e-4f)
    {
        return std::abs(x.x - y.x) <= tolerance && std::abs(x.y - y.y) <= tolerance && std::abs(x.z - y.z) <= tolerance;
    }

    // The lines of a batch, two vertices each.
    inline std::vector<Segment> Segments(const Fenrir::DebugLineBatch& batch)
    {
        std::vector<Segment> segments;
        for (std::size_t i = 0; i + 1 < batch.vertices.size(); i += 2)
        {
            segments.push_back({batch.vertices[i].position, batch.vertices[i + 1].position});
        }
        return segments;
    }

    // Whether `actual` holds the same lines as `expected`, in any order and either direction.
    inline bool SameSegments(std::vector<Segment> actual, const std::vector<Segment>& expected)
    {
        if (actual.size() != expected.size())
        {
            return false;
        }
        for (const Segment& want : expected)
        {
            bool found = false;
            for (std::size_t i = 0; i < actual.size(); ++i)
            {
                const Segment& have = actual[i];
                if ((Near(have.a, want.a) && Near(have.b, want.b)) || (Near(have.a, want.b) && Near(have.b, want.a)))
                {
                    actual.erase(actual.begin() + static_cast<std::ptrdiff_t>(i));
                    found = true;
                    break;
                }
            }
            if (!found)
            {
                return false;
            }
        }
        return true;
    }

    // How many lines every line batch holds together.
    inline std::size_t LineCount(const Fenrir::DebugBatches& batches)
    {
        std::size_t count = 0;
        for (const Fenrir::DebugLineBatch& batch : batches.lines)
        {
            count += batch.vertices.size() / 2;
        }
        return count;
    }
} // namespace DebugDrawTest
