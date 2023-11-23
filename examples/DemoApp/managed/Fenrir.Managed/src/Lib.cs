using System;

namespace Fenrir.Managed
{

    public static class Lib
    {
        private static int s_CallCount = 1;

        public static int Hello()
        {
            Console.WriteLine($"Hello, world! from {nameof(Lib)} [count: {s_CallCount++}]");
            return 0;
        }
    }
}
