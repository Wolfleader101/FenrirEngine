#pragma once

#include <memory>

#include "FenrirInput/Devices.hpp"
#include "FenrirInput/InputEvents.hpp"
#include "FenrirInput/InputFeed.hpp"
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
     * @brief The owner of FenrirInput in an app (ADR-0017): the Keyboard, Mouse, Gamepads and InputFeed app resources
     * and the dispatch system that fills them. Move-only.
     *
     * Input links no library and needs no window: with no FenrirWindow, the only input is what code feeds through the
     * InputFeed. While an owner lives, the app has the four resources; when the last owner of the app's input is
     * destroyed they are removed and the dispatch does nothing. Destroy it before the App.
     */
    class Input
    {
      public:
        /**
         * @brief Turns input on for `app`, or shares the input it already has: a second Install while an owner lives
         * returns another owner of the same input, so FenrirWindow can install it whether or not the game did. Adds
         * the Keyboard, Mouse, Gamepads and InputFeed app resources and the dispatch, a sequential PreUpdate system
         * (it runs on the main thread, since input routers will be called from it and may reach SDL). Cannot fail.
         */
        [[nodiscard]] static Input Install(App& app);

        /**
         * @brief Dispatches everything fed to `app`'s input feed since the last dispatch: updates the Keyboard, Mouse
         * and Gamepads and sends each input as its event, in the order fed. Does nothing with nothing pending, or when
         * the app has no input. FenrirInput's own PreUpdate system calls it; a feeder that runs after that system, such
         * as FenrirWindow's pump, calls it right after feeding, so its input reaches the game in the same frame
         * whichever package was installed first. Call it from the thread that runs the App, outside parallel systems.
         */
        static void Dispatch(App& app);

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
