#include <iostream>

#include "FenrirApp/App.hpp"

int main()
{
    std::cout << "hello world" << std::endl;

    Fenrir::App app;
    app.Init().AddSystem("test").AddSystem("abc").AddSystem("render").AddSystem("xyz").AddSystem("yes").Run();
}