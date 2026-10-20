using System.Runtime.InteropServices;
using Fenrir.Scripting;

namespace CompileFail;

public enum MotionType : byte
{
    Still,
    Spin,
}

// An empty kind, one unused byte, as a C++ struct Still {} is.
public struct Still
{
}

public struct Spin
{
    public float Rate;
}

[TaggedUnion(Noun = "motion")]
[StructLayout(LayoutKind.Explicit)]
public partial struct Motion
{
    [FieldOffset(0)] private MotionType _type;
    [FieldOffset(4)] private Still _still;
    [FieldOffset(4)] private Spin _spin;
}

public static class UseMotion
{
    public static bool Use()
    {
        Motion motion = new Still();
        ref Still still = ref motion.Still;
        motion = new Spin { Rate = 1 };
        return motion.Type == MotionType.Spin && new Motion().Type == MotionType.Still && still.Equals(default(Still));
    }
}
