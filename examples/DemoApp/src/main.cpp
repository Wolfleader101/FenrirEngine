#include <iostream>

#include "FenrirApp/App.hpp"
#include "FenrirLogger/ConsoleLogger.hpp"
#include "FenrirScripting/Host.hpp"

static void systemA(Fenrir::App& app)
{
    app.Logger()->Warn("System A {0}", 1);
}

static void systemB(Fenrir::App& app)
{
    app.Logger()->Error("System B");
}

static void Tick(Fenrir::App& app)
{
    app.Logger()->Log("Tick");
}

static void PreInit(Fenrir::App& app)
{
    app.Logger()->Info("PreInit");
}

static void Init(Fenrir::App& app)
{
    app.Logger()->Info("Init");
    InitHost(app);
}

static void PostInit(Fenrir::App& app)
{
    app.Logger()->Fatal("PostInit");
}

int main()
{
    auto logger = std::make_unique<Fenrir::ConsoleLogger>();
    Fenrir::App app(std::move(logger));

    app.AddSystems(Fenrir::SchedulePriority::PreInit, {PreInit})
        .AddSystems(Fenrir::SchedulePriority::Init, {Init})
        // .AddSystems(Fenrir::SchedulePriority::PostInit, {PostInit})
        // .AddSystems(Fenrir::SchedulePriority::Update, {systemB})
        // .AddSystems(Fenrir::SchedulePriority::PreUpdate, {systemA})
        // .AddSystems(Fenrir::SchedulePriority::Tick, {Tick})
        .Run();
}