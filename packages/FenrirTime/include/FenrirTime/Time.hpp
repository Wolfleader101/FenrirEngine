#pragma once

#include <chrono>

namespace Fenrir
{

    struct Time
    {
        Time();

        double deltaTime = 0.0;
        // int fps;
        double tickRate = 1.0 / 60.0;
        double accumulator = 0.0;

        void Update();

      private:
        std::chrono::steady_clock::time_point prevTime;
        // int frameCount = 0;
        // double prevFPSTime = 0.0;
    };
} // namespace Fenrir