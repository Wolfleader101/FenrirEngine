#pragma once

#include <chrono>

namespace Fenrir
{

    struct Time
    {
        Time();

        double deltaTime = 0.0;
        double tickRate = 1.0 / 60.0;
        double accumulator = 0.0;

        /**
         * @brief update the time
         *
         */
        void Update();

        /**
         * @brief get the time since the start of the application
         *
         * @return double the time since the start of the application
         */
        double CurrentTime() const;

      private:
        std::chrono::steady_clock::time_point startTime;
        std::chrono::steady_clock::time_point prevTime;
    };
} // namespace Fenrir