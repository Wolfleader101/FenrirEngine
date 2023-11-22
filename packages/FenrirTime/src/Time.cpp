#include "FenrirTime/Time.hpp"

namespace Fenrir
{
    Time::Time() : prevTime(std::chrono::steady_clock::now())
    {
    }

    void Time::Update()
    {
        auto now = std::chrono::steady_clock::now();
        deltaTime = std::chrono::duration<double>(now - prevTime).count();
        prevTime = now;

        accumulator += deltaTime;
        // frameCount++;

        // // Update FPS every second
        // if (currTime - prevFPSTime >= 1.0)
        // {
        //     fps = frameCount;
        //     frameCount = 0;
        //     prevFPSTime = currTime;
        // }
    }
} // namespace Fenrir