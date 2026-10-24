// The reload watcher's decision, with no .NET and no file: when does a changed script assembly count as a finished
// build? The watcher is told the time, what the file looks like, and whether it can be opened, and says Idle, Settling
// or Ready.
#include <catch2/catch_test_macros.hpp>

#include "ReloadWatcher.hpp"

#include <chrono>
#include <optional>

namespace
{
    using Fenrir::Detail::ReloadWatcher;
    using Stamp = ReloadWatcher::Stamp;
    using Watch = ReloadWatcher::Watch;
    using namespace std::chrono_literals;

    // Milliseconds after the clock's start. The watcher begins at the start, so the first look is long enough after it.
    ReloadWatcher::Clock::time_point At(int milliseconds)
    {
        return ReloadWatcher::Clock::time_point{} + std::chrono::milliseconds(milliseconds);
    }

    Stamp Build(int number)
    {
        return Stamp{std::filesystem::file_time_type{} + std::chrono::seconds(number),
                     static_cast<std::uintmax_t>(number)};
    }

    // The file the watcher looks at: what it looks like now, whether it can be opened, and how often it was looked at.
    struct Disk
    {
        std::optional<Stamp> stamp;
        bool openable = true;
        int looks = 0;
        int opens = 0;

        Watch Poll(ReloadWatcher& watcher, int milliseconds)
        {
            return watcher.Poll(
                At(milliseconds),
                [this] {
                    ++looks;
                    return stamp;
                },
                [this] {
                    ++opens;
                    return openable;
                });
        }
    };

    // The defaults the engine uses: look every 250 ms, and a build must stay the same for 400 ms.
    ReloadWatcher Watcher()
    {
        return ReloadWatcher({.hotReload = true, .interval = 250ms, .debounce = 400ms});
    }
} // namespace

TEST_CASE("a look sooner than the interval after the last is Idle and does not look at the file", "[reload-watcher]")
{
    auto watcher = Watcher();
    Disk disk{Build(2)};
    INFO("The first look should find a new build");
    REQUIRE(disk.Poll(watcher, 1000) == Watch::Settling);

    INFO("A look 100 ms later is inside the 250 ms interval");
    REQUIRE(disk.Poll(watcher, 1100) == Watch::Idle);
    INFO("The file should not have been looked at again");
    REQUIRE(disk.looks == 1);
}

TEST_CASE("a look exactly one interval after the last does look at the file", "[reload-watcher]")
{
    auto watcher = Watcher();
    Disk disk{Build(2)};
    REQUIRE(disk.Poll(watcher, 1000) == Watch::Settling);

    INFO("250 ms is the interval, so this look is allowed");
    REQUIRE(disk.Poll(watcher, 1250) == Watch::Settling);
    REQUIRE(disk.looks == 2);
}

TEST_CASE("an interval of zero looks at the file on every poll", "[reload-watcher]")
{
    ReloadWatcher watcher({.hotReload = true, .interval = 0ms, .debounce = 400ms});
    Disk disk{Build(2)};
    REQUIRE(disk.Poll(watcher, 1000) == Watch::Settling);

    INFO("Polls 1 ms apart should each look");
    REQUIRE(disk.Poll(watcher, 1001) == Watch::Settling);
    REQUIRE(disk.Poll(watcher, 1002) == Watch::Settling);
    REQUIRE(disk.looks == 3);
}

TEST_CASE("a file that looks like the loaded one is Idle", "[reload-watcher]")
{
    auto watcher = Watcher();
    watcher.MarkLoaded(Build(1));
    Disk disk{Build(1)};

    INFO("The loaded build should need no reload");
    REQUIRE(disk.Poll(watcher, 1000) == Watch::Idle);
}

TEST_CASE("a file that cannot be looked at is Idle", "[reload-watcher]")
{
    auto watcher = Watcher();
    watcher.MarkLoaded(Build(1));
    Disk disk{std::nullopt};

    INFO("A missing file should wait, not reload");
    REQUIRE(disk.Poll(watcher, 1000) == Watch::Idle);
}

TEST_CASE("the first sight of a new build is Settling", "[reload-watcher]")
{
    auto watcher = Watcher();
    watcher.MarkLoaded(Build(1));
    Disk disk{Build(2)};

    INFO("A changed file may still be being written");
    REQUIRE(disk.Poll(watcher, 1000) == Watch::Settling);
}

TEST_CASE("a build that changes again while settling restarts the wait", "[reload-watcher]")
{
    auto watcher = Watcher();
    watcher.MarkLoaded(Build(1));
    Disk disk{Build(2)};
    REQUIRE(disk.Poll(watcher, 1000) == Watch::Settling);
    disk.stamp = Build(3);
    INFO("A different stamp is a build still being written");
    REQUIRE(disk.Poll(watcher, 1300) == Watch::Settling);

    INFO("300 ms after the last change is still inside the 400 ms debounce, however long ago the first change was");
    REQUIRE(disk.Poll(watcher, 1600) == Watch::Settling);
    INFO("Past the 400 ms debounce after the last change (and the look interval) it is ready");
    REQUIRE(disk.Poll(watcher, 1900) == Watch::Ready);
}

TEST_CASE("a build that stays the same for less than the debounce is Settling", "[reload-watcher]")
{
    auto watcher = Watcher();
    watcher.MarkLoaded(Build(1));
    Disk disk{Build(2)};
    REQUIRE(disk.Poll(watcher, 1000) == Watch::Settling);

    INFO("300 ms is under the 400 ms debounce");
    REQUIRE(disk.Poll(watcher, 1300) == Watch::Settling);
    INFO("The file should not be opened until the debounce has passed");
    REQUIRE(disk.opens == 0);
}

TEST_CASE("a build that cannot be opened is Settling even after the debounce", "[reload-watcher]")
{
    auto watcher = Watcher();
    watcher.MarkLoaded(Build(1));
    Disk disk{Build(2)};
    disk.openable = false;
    REQUIRE(disk.Poll(watcher, 1000) == Watch::Settling);

    INFO("Something is still holding the file");
    REQUIRE(disk.Poll(watcher, 1500) == Watch::Settling);
    disk.openable = true;
    INFO("Once it can be opened it is ready");
    REQUIRE(disk.Poll(watcher, 1800) == Watch::Ready);
}

TEST_CASE("a build that stays the same for the debounce and can be opened is Ready", "[reload-watcher]")
{
    auto watcher = Watcher();
    watcher.MarkLoaded(Build(1));
    Disk disk{Build(2)};
    REQUIRE(disk.Poll(watcher, 1000) == Watch::Settling);

    INFO("400 ms after it was first seen, unchanged and readable");
    REQUIRE(disk.Poll(watcher, 1400) == Watch::Ready);
}

TEST_CASE("a file that goes back to the loaded build clears what was pending", "[reload-watcher]")
{
    auto watcher = Watcher();
    watcher.MarkLoaded(Build(1));
    Disk disk{Build(2)};
    REQUIRE(disk.Poll(watcher, 1000) == Watch::Settling);
    disk.stamp = Build(1);
    INFO("The loaded build is back");
    REQUIRE(disk.Poll(watcher, 1300) == Watch::Idle);

    disk.stamp = Build(2);
    INFO("A new sight of the earlier build starts the wait again");
    REQUIRE(disk.Poll(watcher, 1600) == Watch::Settling);
    INFO("It must not be ready on the strength of the earlier sight");
    REQUIRE(disk.Poll(watcher, 1900) == Watch::Settling);
}

TEST_CASE("with hot reload off the watcher is always Idle and never looks", "[reload-watcher]")
{
    ReloadWatcher watcher({.hotReload = false, .interval = 250ms, .debounce = 400ms});
    watcher.MarkLoaded(Build(1));
    Disk disk{Build(2)};

    INFO("A changed file should be ignored");
    REQUIRE(disk.Poll(watcher, 1000) == Watch::Idle);
    REQUIRE(disk.Poll(watcher, 5000) == Watch::Idle);
    INFO("The file should not have been looked at");
    REQUIRE(disk.looks == 0);
}

TEST_CASE("marking a build as loaded resets what was pending", "[reload-watcher]")
{
    auto watcher = Watcher();
    watcher.MarkLoaded(Build(1));
    Disk disk{Build(2)};
    REQUIRE(disk.Poll(watcher, 1000) == Watch::Settling);

    // A reload by hand loads that build, so the watcher must not reload it a second time.
    watcher.MarkLoaded(Build(2));

    INFO("The build that is now loaded should need no reload");
    REQUIRE(disk.Poll(watcher, 1500) == Watch::Idle);
}
