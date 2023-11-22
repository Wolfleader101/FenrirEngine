#include <iostream>

#include "FenrirApp/App.hpp"

static void systemA()
{
    std::cout << "System A" << std::endl;
}

static void systemB()
{
    std::cout << "System B" << std::endl;
}

static void PreInit()
{
    std::cout << "PreInit" << std::endl;
}

static void Init()
{
    std::cout << "Init" << std::endl;
}

static void PostInit()
{
    std::cout << "PostInit" << std::endl;
}

int main()
{
    std::cout << "hello world" << std::endl;

    Fenrir::App app;
    app.AddSystems(Fenrir::SchedulePriority::PreInit, {PreInit})
        .AddSystems(Fenrir::SchedulePriority::Init, {Init})
        .AddSystems(Fenrir::SchedulePriority::PostInit, {PostInit})
        .AddSystems(Fenrir::SchedulePriority::Update, {systemB})
        .AddSystems(Fenrir::SchedulePriority::PreUpdate, {systemA})
        .Run();
}