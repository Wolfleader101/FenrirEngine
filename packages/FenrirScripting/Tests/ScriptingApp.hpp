#pragma once

// The built-in bindings' generated defaults tests' (Generated/) part of FenrirScripting's ExtensionTestApp: the fixture
// CTest names in FENRIR_TEST_FIXTURE, no packages installed (an App with no window), and no extension but the one
// listing the fixture's asset kinds: the built-in bindings are scripting's own.

#include "ExtensionTestApp.hpp"
#include "FixtureCalls.hpp"

namespace Fenrir::Testing
{
    struct ScriptingApp : ExtensionTestApp<NoPackages>
    {
        ScriptingApp()
            : ExtensionTestApp(FixtureFromEnvironment("FENRIR_TEST_FIXTURE"), {ScriptFixture::FixtureAssetKinds()})
        {
        }
    };
} // namespace Fenrir::Testing
