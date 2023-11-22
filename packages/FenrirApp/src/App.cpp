#include "FenrirApp/App.hpp"

#include <iostream>

namespace Fenrir
{
    App::App()
    {
        std::cout << "hello from app" << std::endl;
        std::cout << "Constructor X = " << x << std::endl;
        x++;
    }

    App& App::Init()
    {
        std::cout << "Init X = " << x << std::endl;
        x++;
        return *this;
    }

    App& App::AddSystem(std::string test)
    {
        std::cout << "hello from system " << test << std::endl;
        std::cout << "AddSystem X = " << x << std::endl;
        x++;
        return *this;
    }

    void App::Run()
    {
        std::cout << "hello from run" << std::endl;
        std::cout << "Run X = " << x << std::endl;
        x++;
    }

} // namespace Fenrir