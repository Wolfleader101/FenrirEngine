#pragma once

// The DemoApp's scene switcher: one F-key per feature family. A family's key steps through the scenes registered for
// it, in the order they were registered, and then back to the home scene (Default); from a scene of another family it
// jumps to that family's first scene. A key with no scenes registered for it does nothing, so a family whose scenes
// were not built in (no scripting, say) loses only its own scenes. Scenes read their own keys in every scene and act
// only while they are active, so a key pressed in one scene does nothing in another.
//
// For running a scene without a keyboard: FENRIR_DEMO_SCENE=<name> starts in that scene, and with FENRIR_DEMO_KEYS=1
// the switcher presses each scene's registered keys after it becomes active, two seconds in and then one a second, and
// stops the app three seconds after the last. They are sent as the window sends them, as KeyboardKeyEvents, so C++ and
// C# readers both see them. The seconds are simulation time (frame deltas), so a run sped up with FENRIR_DEMO_FAST
// presses them sooner. scripts/demo-smoke.sh runs every physics scene this way at once.
//
// DemoApp code, not engine code: the engine knows scenes, not feature families.

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

#include "Events.hpp"
#include "FenrirApp/App.hpp"

namespace DemoFamilyKey
{
    // GLFW's key codes, without GLFW's header (it must follow glad's).
    inline constexpr int physicsCSharp = 292;       // F3
    inline constexpr int joints = 293;              // F4
    inline constexpr int characterController = 294; // F5
    inline constexpr int vehicles = 295;            // F6
    inline constexpr int ragdolls = 296;            // F7
    inline constexpr int softBodies = 297;          // F8
} // namespace DemoFamilyKey

/** @brief A scene the switcher steps to. The App must have it by the first PreUpdate. */
struct DemoScene
{
    std::string name;
    int familyKey = 0;          // One of DemoFamilyKey.
    std::string keyHelp;        // One line, logged each time the scene becomes active.
    std::vector<int> smokeKeys; // Pressed in turn when FENRIR_DEMO_KEYS=1, as key codes.
};

class DemoScenes
{
  public:
    /** @brief `home` is the scene the App starts in and every family's key returns to; `homeKeyHelp` is logged on
     * entering it, followed by each registered scene's F-key. */
    DemoScenes(std::string home, std::string homeKeyHelp)
        : m_home(std::move(home)), m_homeKeyHelp(std::move(homeKeyHelp))
    {
        const char* smokeRun = std::getenv("FENRIR_DEMO_KEYS");
        m_smokeRun = smokeRun != nullptr && std::string(smokeRun) == "1";
    }

    /** @brief Adds a scene to its family's steps, after the family's scenes already registered. Before the App runs. */
    void Register(DemoScene scene)
    {
        m_scenes.push_back(std::move(scene));
    }

    /** @brief In PreUpdate, before anything of the frame has run: switches on a family key, logs the key help of a
     * scene that has just become active (however it did), and presses its keys in a smoke run. */
    void Update(Fenrir::App& app)
    {
        if (!m_started)
        {
            m_started = true;
            StartInRequestedScene(app);
        }

        for (const KeyboardKeyEvent& event : m_keys.Read(app))
        {
            if (event.state != InputState::Pressed)
            {
                continue;
            }
            const std::string next = NextInFamily(app.GetActiveScene().GetName(), event.key);
            if (!next.empty())
            {
                app.ChangeActiveScene(next);
            }
        }

        const std::string& active = app.GetActiveScene().GetName();
        if (active != m_active)
        {
            m_active = active;
            m_secondsActive = 0.0;
            m_smokeKeysPressed = 0;
            LogEntry(app);
        }

        PressSmokeKeys(app);
    }

  private:
    // FENRIR_DEMO_SCENE, if set, names the scene to start in: the home scene or a registered one.
    void StartInRequestedScene(Fenrir::App& app) const
    {
        const char* requested = std::getenv("FENRIR_DEMO_SCENE");
        if (requested == nullptr || requested == m_home)
        {
            return;
        }
        if (Find(requested) == nullptr)
        {
            app.Logger()->Warn("FENRIR_DEMO_SCENE: no scene \"{}\" in this build; starting in {}.", requested, m_home);
            return;
        }
        app.ChangeActiveScene(requested);
    }

    // Where `key` goes from `active`: the family's next scene, its first when `active` is not one of the family's, or
    // home after its last. Empty when no scene is registered for `key`.
    std::string NextInFamily(const std::string& active, int key) const
    {
        std::vector<const DemoScene*> family;
        for (const DemoScene& scene : m_scenes)
        {
            if (scene.familyKey == key)
            {
                family.push_back(&scene);
            }
        }
        if (family.empty())
        {
            return {};
        }
        for (std::size_t i = 0; i < family.size(); ++i)
        {
            if (family[i]->name == active)
            {
                return i + 1 < family.size() ? family[i + 1]->name : m_home;
            }
        }
        return family.front()->name;
    }

    void LogEntry(Fenrir::App& app) const
    {
        if (const DemoScene* scene = Find(m_active))
        {
            app.Logger()->Info("Scene: {}. {}", scene->name, scene->keyHelp);
            return;
        }
        // Home's help ends with where each scene is, as only the scenes built in are registered.
        std::string scenes;
        for (const DemoScene& scene : m_scenes)
        {
            scenes += (scenes.empty() ? " Scenes: F" : ", F") + std::to_string(scene.familyKey - 289) + " " +
                      scene.name; // GLFW_KEY_F1 is 290.
        }
        app.Logger()->Info("Scene: {}. {}{}{}", m_active, m_homeKeyHelp, scenes, scenes.empty() ? "" : ".");
    }

    // Two seconds after entry, to let the scene settle, then one key a second. Three seconds after the last, for its
    // effects to log, the app stops, so a smoke run ends by itself.
    void PressSmokeKeys(Fenrir::App& app)
    {
        const DemoScene* scene = m_smokeRun ? Find(m_active) : nullptr;
        if (scene == nullptr)
        {
            return;
        }
        // A long frame counts as a short one: the first frames' deltas include loading, which is not settling.
        m_secondsActive += std::min(app.GetTime().deltaTime, 0.1);
        const std::size_t keyCount = scene->smokeKeys.size();
        if (m_smokeKeysPressed >= keyCount)
        {
            if (m_secondsActive >= 2.0 + static_cast<double>(keyCount) + 3.0)
            {
                app.Logger()->Info("FENRIR_DEMO_KEYS: pressed all {} keys in {}; stopping.", keyCount, scene->name);
                app.Stop();
            }
            return;
        }
        if (m_secondsActive < 2.0 + static_cast<double>(m_smokeKeysPressed))
        {
            return;
        }
        const int key = scene->smokeKeys[m_smokeKeysPressed++];
        app.Logger()->Info("FENRIR_DEMO_KEYS: pressing key {} ({}/{}) in {}.", key, m_smokeKeysPressed, keyCount,
                           scene->name);
        app.SendEvent(KeyboardKeyEvent{key, 0, 0, InputState::Pressed, 0});
        app.SendEvent(KeyboardKeyEvent{key, 0, 0, InputState::Released, 0});
    }

    const DemoScene* Find(const std::string& name) const
    {
        for (const DemoScene& scene : m_scenes)
        {
            if (scene.name == name)
            {
                return &scene;
            }
        }
        return nullptr;
    }

    std::string m_home;
    std::string m_homeKeyHelp;
    std::vector<DemoScene> m_scenes;
    Fenrir::EventReader<KeyboardKeyEvent> m_keys;
    bool m_smokeRun = false;
    bool m_started = false;
    std::string m_active; // The active scene as of the last Update, to notice a change however it was made.
    double m_secondsActive = 0.0;
    std::size_t m_smokeKeysPressed = 0;
};
