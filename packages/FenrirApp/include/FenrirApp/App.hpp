#pragma once

#include <memory>

#include "FenrirLogger/ILogger.hpp"
#include "FenrirScheduler/Scheduler.hpp"
#include "FenrirTime/Time.hpp"

namespace Fenrir
{
    class App
    {
      public:
        App(std::unique_ptr<ILogger> logger);

        App& AddSystems(SchedulePriority priority, std::initializer_list<SystemFunc> systems);

        App& AddSystem(SchedulePriority priority, SystemFunc system);

        const std::unique_ptr<ILogger>& Logger() const;

        void Run();

        void Stop();

      private:
        Time m_time;
        Scheduler m_scheduler;
        std::unique_ptr<ILogger> m_logger;
        bool m_running = true;
    };

} // namespace Fenrir