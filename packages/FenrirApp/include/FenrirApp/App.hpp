#include <string>

namespace Fenrir
{
    class App
    {
      public:
        App();

        App& Init();

        App& AddSystem(std::string test);

        void Run();

      private:
        int x = 0;
    };
} // namespace Fenrir