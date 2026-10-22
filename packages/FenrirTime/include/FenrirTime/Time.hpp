#pragma once

#include <chrono>
#include <cstdint>

namespace Fenrir
{

    struct Time
    {
        Time();

        double deltaTime = 0.0;
        double tickRate = 1.0 / 60.0;
        double accumulator = 0.0;
        /**
         * @brief The most fixed steps one frame runs. Time owed beyond them is dropped from the accumulator, with a
         * warning, so a long frame slows the game briefly instead of spiralling.
         */
        int maxTicksPerFrame = 5;

        /**
         * @brief The frames begun so far: during a frame, its number, from 1 (0 before the first frame, in the init
         * phases). The App counts it as each frame begins, before PreUpdate.
         */
        std::uint64_t frameCount = 0;

        /**
         * @brief The fixed steps begun so far: during PreTick, Tick and PostTick, the step's number, from 1. It keeps
         * the last step's number through a frame with no step (0 before the first). The App counts it as each step
         * begins, before its PreTick. With frameCount, what a package records can be stamped with the step or frame it
         * belongs to, and compared when read.
         */
        std::uint64_t tickCount = 0;

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