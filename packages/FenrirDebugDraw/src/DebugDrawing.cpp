#include "FenrirDebugDraw/DebugDrawing.hpp"

#include <utility>

#include "DebugDrawState.hpp"
#include "FenrirApp/App.hpp"
#include "FenrirScene/Scene.hpp"

namespace Fenrir
{
    DebugDrawing::DebugDrawing(std::shared_ptr<Detail::DebugDrawState> state) : m_state(std::move(state))
    {
    }

    DebugDrawing::DebugDrawing(DebugDrawing&& other) noexcept = default;

    DebugDrawing& DebugDrawing::operator=(DebugDrawing&& other) noexcept
    {
        if (this != &other)
        {
            Shutdown();
            m_state = std::move(other.m_state);
        }
        return *this;
    }

    DebugDrawing::~DebugDrawing()
    {
        Shutdown();
    }

    bool DebugDrawing::IsValid() const noexcept
    {
        return m_state != nullptr;
    }

    void DebugDrawing::Shutdown() noexcept
    {
        if (!m_state)
        {
            return;
        }
        // Every scene's DebugDraw refers to the state, so they go first.
        m_state->app->ForEachScene([](Scene& scene) { scene.RemoveResource<DebugDraw>(); });
        m_state.reset();
    }

    DebugDrawing DebugDrawing::Install(App& app)
    {
        auto state = std::make_shared<Detail::DebugDrawState>(app);
        app.ForEachScene([&state](Scene& scene) { scene.AddResource<DebugDraw>(*state); });
        app.AddSequentialSystem(SchedulePriority::PreUpdate,
                                [installed = std::weak_ptr<Detail::DebugDrawState>(state)](App& running) {
                                    const std::shared_ptr<Detail::DebugDrawState> current = installed.lock();
                                    if (!current)
                                    {
                                        return;
                                    }
                                    const double deltaTime = running.GetTime().deltaTime;
                                    running.ForEachScene([&current, deltaTime](Scene& scene) {
                                        if (DebugDraw* draw = scene.GetResource<DebugDraw>())
                                        {
                                            draw->Expire(deltaTime);
                                        }
                                        else
                                        {
                                            scene.AddResource<DebugDraw>(*current);
                                        }
                                    });
                                });
        return DebugDrawing(std::move(state));
    }
} // namespace Fenrir
