#include "FenrirAnimation/Animation.hpp"

#include <utility>

#include "AnimationState.hpp"
#include "Animators.hpp"
#include "FenrirApp/App.hpp"
#include "FenrirScene/Scene.hpp"
#include "FenrirSkeleton/Skeletons.hpp"

namespace Fenrir
{
    namespace
    {
        Detail::Animators& AnimatorsOf(Scene& scene)
        {
            if (Detail::Animators* animators = scene.GetResource<Detail::Animators>())
            {
                return *animators;
            }
            return scene.AddResource<Detail::Animators>();
        }
    } // namespace

    Animation Animation::Install(App& app, Skeletons& skeletons, AnimationConfig config)
    {
        if (!skeletons.IsValid())
        {
            app.Logger()->Error("{}", "Animation::Install: FenrirSkeleton is not installed (the Skeletons owner is not "
                                      "valid), so FenrirAnimation is off: call Skeletons::Install first");
            return Animation(nullptr);
        }
        if (app.TryGetResource<Detail::AnimationInstalled>() != nullptr)
        {
            app.Logger()->Error("{}", "Animation::Install: FenrirAnimation is already installed in this app; this "
                                      "second install is ignored and adds nothing");
            return Animation(nullptr);
        }
        auto state = std::make_shared<Detail::AnimationState>(app, config);
        app.AddResource<Detail::AnimationInstalled>(Detail::AnimationInstalled{state.get()});
        // Held weakly: once the owner is gone the sampler does nothing, whether or not FenrirSkeleton outlives it.
        skeletons.AddAnimate("FenrirAnimation", AnimateOrder::Clips,
                             [installed = std::weak_ptr<Detail::AnimationState>(state)](AnimateContext& context) {
                                 if (const std::shared_ptr<Detail::AnimationState> current = installed.lock())
                                 {
                                     AnimatorsOf(context.GetScene()).Run(*current, context);
                                 }
                             });
        return Animation(std::move(state));
    }

    Animation::Animation(std::shared_ptr<Detail::AnimationState> state) noexcept : m_state(std::move(state))
    {
    }

    Animation::Animation(Animation&& other) noexcept = default;

    Animation& Animation::operator=(Animation&& other) noexcept
    {
        if (this != &other)
        {
            Shutdown();
            m_state = std::move(other.m_state);
        }
        return *this;
    }

    Animation::~Animation()
    {
        Shutdown();
    }

    bool Animation::IsValid() const noexcept
    {
        return m_state != nullptr;
    }

    void Animation::Shutdown() noexcept
    {
        if (!m_state)
        {
            return;
        }
        App& app = *m_state->app;
        const Detail::AnimationInstalled* installed = app.TryGetResource<Detail::AnimationInstalled>();
        if (installed != nullptr && installed->state == m_state.get())
        {
            app.RemoveResource<Detail::AnimationInstalled>();
        }
        app.ForEachScene([](Scene& scene) { scene.RemoveResource<Detail::Animators>(); });
        m_state.reset();
    }

    void Animation::Seek(Scene& scene, Entity entity, double seconds)
    {
        if (entity.IsValid())
        {
            AnimatorsOf(scene).Seek(entity, seconds);
        }
    }

    PlaybackInfo Animation::Playback(Scene& scene, Entity entity)
    {
        PlaybackInfo info;
        const Detail::Animators* animators = scene.GetResource<Detail::Animators>();
        if (animators == nullptr || !entity.IsValid() || !entity.HasComponent<Animator>())
        {
            return info;
        }
        const PoseState pose = Skeletons::GetPoseState(scene, entity);
        info.state = pose;
        if (const Detail::AnimatorPlayback* playback = animators->Find(entity))
        {
            info.time = playback->cursor;
            info.completedLoops = playback->completedLoops;
            info.finished = playback->finished;
            if (pose == PoseState::Ready && playback->unavailable)
            {
                info.state = PoseState::Unavailable;
            }
        }
        return info;
    }
} // namespace Fenrir
