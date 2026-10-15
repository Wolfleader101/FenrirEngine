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
         * @brief Moves time on by `seconds`: it becomes this frame's delta and is added to the accumulator.
         * Reads no clock.
         */
        void Advance(double seconds);

        /**
         * @brief Seconds of wall-clock time since the last call (or since construction), and starts measuring again.
         */
        double MeasureFrame();

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