#include "FenrirTime/Time.hpp"

namespace Fenrir
{
    Time::Time()
    {
        startTime = prevTime = std::chrono::steady_clock::now();
    }

    void Time::Update()
    {
        auto now = std::chrono::steady_clock::now();
        deltaTime = std::chrono::duration<double>(now - prevTime).count();
        prevTime = now;

        accumulator += deltaTime;
    }

    double Time::CurrentTime() const
    {
        auto now = std::chrono::steady_clock::now();
        return std::chrono::duration<double>(now - startTime).count();
    }
} // namespace Fenrir