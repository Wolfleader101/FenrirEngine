#pragma once

#include <memory>

#include "FenrirInput/Devices.hpp"
#include "FenrirInput/InputFeed.hpp"

namespace Fenrir
{
    class App;

    namespace Detail
    {
        /** @brief What Input::Install set up for one app, shared by every owner of its input. */
        struct InputInstallation
        {
            explicit InputInstallation(App& app);
            InputInstallation(const InputInstallation&) = delete;
            InputInstallation& operator=(const InputInstallation&) = delete;
            // Removes the app's input resources.
            ~InputInstallation();

            App* app;
        };

        /** @brief The app resource through which a second Install finds the input already installed. */
        struct InstalledInput
        {
            std::weak_ptr<InputInstallation> installation;
        };

        /**
         * @brief FenrirInput's dispatch: takes what was fed, in order, and passes each input to the game, updating the
         * Keyboard, Mouse and Gamepads and sending it as its event. Input routers (ADR-0027) will sit between the feed
         * and Pass, deciding which input reaches it.
         */
        class InputDispatch
        {
          public:
            /** @brief Dispatches everything fed since the last dispatch. Runs in PreUpdate, sequentially. */
            static void Run(App& app);

          private:
            /** @brief Lets `input` reach game state: the resources, then its event. */
            static void Pass(App& app, const FedInput& input);
        };
    } // namespace Detail
} // namespace Fenrir
