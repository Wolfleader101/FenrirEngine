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

int main()
{
    std::cout << "hello world" << std::endl;

    Fenrir::App app;
    app.AddSystems(Fenrir::SchedulePriority::Init, {systemA, systemB}).Run();
}