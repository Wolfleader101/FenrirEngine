#include "FenrirTime/Time.hpp"

namespace Fenrir
{
    Time::Time()
    {
        startTime = prevTime = std::chrono::steady_clock::now();
    }

    void Time::Advance(double seconds)
    {
        deltaTime = seconds;
        accumulator += seconds;
    }

    double Time::MeasureFrame()
    {
        auto now = std::chrono::steady_clock::now();
        const double elapsed = std::chrono::duration<double>(now - prevTime).count();
        prevTime = now;
        return elapsed;
    }

    double Time::CurrentTime() const
    {
        auto now = std::chrono::steady_clock::now();
        return std::chrono::duration<double>(now - startTime).count();
    }
} // namespace Fenrir