#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "FenrirInput/Devices.hpp"
#include "FenrirInput/InputFeed.hpp"
#include "FenrirInput/InputRouter.hpp"

namespace Fenrir
{
    class App;

    namespace Detail
    {
        /** @brief One registered input router and what it publishes. */
        struct RouterEntry
        {
            InputRouterId id;
            int priority = 0;
            std::shared_ptr<InputRouter> router;
            /** @brief Removed during a dispatch: skipped from then on, and dropped once the dispatch ends. */
            bool removed = false;
            std::vector<std::pair<WindowId, WindowInputClaims>> claims;
            bool gamepadClaim = false;
        };

        /**
         * @brief A held control and its control owner: a key (by scancode, or by key code when the scancode is
         * unknown), a mouse button or a gamepad button, from its press until its physical release.
         */
        struct OwnedControl
        {
            enum class Device : std::uint8_t
            {
                Keyboard,
                Mouse,
                Gamepad,
            };

            Device device = Device::Keyboard;
            std::uint32_t code = 0;
            GamepadId gamepad;
            /** @brief The window the press came from; it does not matter for a gamepad button. */
            WindowId window;
            /** @brief The router that consumed the press, or the zero id for the game. */
            InputRouterId owner;
            /** @brief Ended early (cancelled, or its router removed): its repeats and release go to no one. */
            bool ended = false;
            /** @brief The press itself, from which a cancelled release is made. */
            FedInput press;
        };

        /** @brief The routers, the control owners and the dispatch's progress through the frames. */
        struct InputRouting
        {
            /** @brief The routers dispatched to, highest priority first, equal priorities in registration order. */
            std::vector<RouterEntry> active;
            /** @brief Routers added since the frame's dispatch began, in registration order: they start next frame. */
            std::vector<RouterEntry> starting;
            std::uint32_t nextId = 1;
            /** @brief The frame whose dispatch has begun (BeginDispatch given), if any has. */
            std::optional<std::uint64_t> begunFrame;
            /** @brief True inside Run, so a router's own call to Input::Dispatch does nothing. */
            bool dispatching = false;
            std::vector<OwnedControl> controls;
        };

        /** @brief What Input::Install set up for one app, shared by every owner of its input. */
        struct InputInstallation
        {
            explicit InputInstallation(App& app);
            InputInstallation(const InputInstallation&) = delete;
            InputInstallation& operator=(const InputInstallation&) = delete;
            // Removes the app's input resources.
            ~InputInstallation();

            App* app;
            InputRouting routing;
        };

        /** @brief The app resource through which a second Install finds the input already installed. */
        struct InstalledInput
        {
            std::weak_ptr<InputInstallation> installation;
        };

        /**
         * @brief FenrirInput's dispatch: takes what was fed, in order, offers each input to the input routers, and
         * passes what they let through to the game, updating the Keyboard, Mouse and Gamepads and sending it as its
         * event (ADR-0027's order: feed, routers, game).
         */
        class InputDispatch
        {
          public:
            /**
             * @brief Dispatches everything fed since the last dispatch (Input::Dispatch). Runs in PreUpdate,
             * sequentially, and may run more than once a frame: from FenrirInput's own system and from a feeder that
             * runs after it, such as FenrirWindow's pump. The frame's first run starts the routers added since and
             * gives each its BeginDispatch, so routers get one a frame, before the frame's first input.
             */
            static void Run(App& app);

            /** @brief Input::AddInputRouter. */
            static InputRouterId AddRouter(App& app, int priority, std::shared_ptr<InputRouter> router);
            /** @brief Input::RemoveInputRouter. */
            static void RemoveRouter(App& app, InputRouterId id);
            /** @brief Input::SetInputClaims. */
            static void SetClaims(App& app, InputRouterId id, WindowId window, const WindowInputClaims& claims);
            /** @brief Input::SetGamepadClaim. */
            static void SetGamepadClaim(App& app, InputRouterId id, bool claimed);

          private:
            /** @brief Lets `input` reach game state: the resources, then its event. */
            static void Pass(App& app, const FedInput& input);

            /** @brief Routes one fed input: to its control owner, or through the routers and on to the game. */
            static void Route(App& app, InputRouting& routing, const FedInput& input);
            /**
             * @brief Offers an unowned input to the routers in order (a captured window's pointer input to its
             * holders only) until one consumes it. The consumer's id, or none when it goes on to the game.
             */
            static std::optional<InputRouterId> Offer(App& app, InputRouting& routing, const FedInput& input,
                                                      WindowId window, bool pointer);
            /** @brief A press: its owner is whoever takes it, the game if no router does. */
            static void Press(App& app, InputRouting& routing, const FedInput& input, OwnedControl control,
                              bool pointer);
            /** @brief A repeat or a release: straight to the press's owner, if the control has not ended. */
            static void Follow(App& app, InputRouting& routing, const FedInput& input, const OwnedControl& control,
                               bool release, bool pointer);
            /** @brief Sends the owner of `routing.controls[index]` a release marked cancelled, and ends the control. */
            static void Cancel(App& app, InputRouting& routing, std::size_t index);
            /** @brief Cancels every owned control in `window` and drops its pointer capture. */
            static void LoseFocus(App& app, InputRouting& routing, WindowId window);
            /** @brief Gives `input` to every router, whatever they answer. */
            static void Broadcast(App& app, InputRouting& routing, const FedInput& input);
            /** @brief Rewrites InputClaims as the union of the routers' claims. */
            static void Publish(App& app, const InputRouting& routing);
        };
    } // namespace Detail
} // namespace Fenrir
