using System;

namespace Fenrir.Managed
{    public static class Lib
    {
        private static int s_CallCount = 1;

        public static int Hello()
        {
            s_CallCount++;
            return s_CallCount;
        }
    }
}
