#pragma once

#include <memory>

#include "FenrirInput/Devices.hpp"
#include "FenrirInput/InputEvents.hpp"
#include "FenrirInput/InputFeed.hpp"
#include "FenrirInput/InputRouter.hpp"
#include "FenrirInput/InputTypes.hpp"

namespace Fenrir
{
    class App;

    namespace Detail
    {
        /** @brief What Input::Install set up. Defined in FenrirInput's sources; nothing else can use it. */
        struct InputInstallation;
    } // namespace Detail

    /**
     * @brief The owner of FenrirInput in an app (ADR-0017): the Keyboard, Mouse, Gamepads, InputFeed and InputClaims
     * app resources, the dispatch system that fills them, and the input routers that see input first (ADR-0027).
     * Move-only.
     *
     * Input links no library and needs no window: with no FenrirWindow, the only input is what code feeds through the
     * InputFeed. While an owner lives, the app has the five resources; when the last owner of the app's input is
     * destroyed they are removed, its routers are dropped and the dispatch does nothing. Destroy it before the App.
     */
    class Input
    {
      public:
        /**
         * @brief Turns input on for `app`, or shares the input it already has: a second Install while an owner lives
         * returns another owner of the same input, so FenrirWindow can install it whether or not the game did. Adds
         * the Keyboard, Mouse, Gamepads, InputFeed and InputClaims app resources and the dispatch, a sequential
         * PreUpdate system (it runs on the main thread, since input routers are called from it and may reach SDL).
         * Cannot fail.
         */
        [[nodiscard]] static Input Install(App& app);

        /**
         * @brief Dispatches everything fed to `app`'s input feed since the last dispatch, in the order fed: each input
         * goes to the input routers, and what they pass updates the Keyboard, Mouse and Gamepads and is sent as its
         * event. The frame's first call also starts the routers added since the last frame and gives every router its
         * BeginDispatch, so each router gets one a frame however many times this is called. Otherwise it does nothing
         * with nothing pending, or when the app has no input. FenrirInput's own PreUpdate system calls it; a feeder
         * that runs after that system, such as FenrirWindow's pump, calls it right after feeding, so its input reaches
         * the game in the same frame whichever package was installed first. Call it from the thread that runs the App,
         * outside parallel systems; a router calling it from its own Route or BeginDispatch does nothing.
         */
        static void Dispatch(App& app);

        /**
         * @brief Registers `router` with `priority` and returns its id. Routers run highest priority first, equal
         * priorities in registration order. It starts at the next frame's dispatch: a router added during a dispatch
         * sees input from the next frame on. Returns the zero id, registering nothing, when the app has no input or
         * `router` is null. Call it from the thread that runs the App, outside parallel systems.
         */
        [[nodiscard]] static InputRouterId AddInputRouter(App& app, int priority, std::shared_ptr<InputRouter> router);

        /**
         * @brief Removes `router` at once: it sees no further input, even in the current dispatch. The controls it owns
         * end silently (no one else ever had them, so their releases go to no one) and its input claims are cleared.
         * An id that names no router does nothing.
         */
        static void RemoveInputRouter(App& app, InputRouterId router);

        /**
         * @brief Publishes what `router` is doing with input in `window`, replacing what it published there before;
         * InputClaims shows the union at once. Raising keyboardFocus cancels the game's held keys in the window, and
         * raising pointerCapture its held mouse buttons: the game gets a release marked `cancelled` at once, and the
         * later physical release goes to no one. Call it from the thread that runs the App, outside parallel systems:
         * from the router's calls, or any sequential system. An id that names no router does nothing.
         */
        static void SetInputClaims(App& app, InputRouterId router, WindowId window, const WindowInputClaims& claims);

        /** @brief Publishes whether `router` claims the gamepads, app-wide, for UI navigation. Cancels nothing. */
        static void SetGamepadClaim(App& app, InputRouterId router, bool claimed);

        Input(Input&& other) noexcept;
        Input& operator=(Input&& other) noexcept;
        Input(const Input&) = delete;
        Input& operator=(const Input&) = delete;
        ~Input();

        /** @brief True for an owner Install returned; false once it is moved from. */
        [[nodiscard]] bool IsValid() const noexcept
        {
            return m_installation != nullptr;
        }

      private:
        explicit Input(std::shared_ptr<Detail::InputInstallation> installation);

        // Shared by every owner of one app's input. The dispatch holds it weakly: once the last owner is gone it does
        // nothing.
        std::shared_ptr<Detail::InputInstallation> m_installation;
    };
} // namespace Fenrir
