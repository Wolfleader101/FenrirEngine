// Links FenrirApp alone. The engine's headers need C++20 (std::string_view and the rest), which this target does not
// set: it builds only if FenrirApp gives it the standard.
#include <memory>

#include "FenrirApp/App.hpp"
#include "FenrirLogger/ConsoleLogger.hpp"

int main()
{
    Fenrir::App app(std::make_unique<Fenrir::ConsoleLogger>());
    return 0;
}
