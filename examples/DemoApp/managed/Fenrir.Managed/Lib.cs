using System;
using System.Runtime.InteropServices;

namespace Fenrir.Managed
{    public static class Lib
    {
        private static int s_CallCount = 1;

        [UnmanagedCallersOnlyAttribute]
        public static void Hello()
        {
            Console.WriteLine($"Hello, world! from {nameof(Lib)} [count: {s_CallCount}]");
            s_CallCount++;
        }
    }
}
