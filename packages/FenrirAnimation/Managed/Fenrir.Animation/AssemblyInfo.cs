using System.Runtime.CompilerServices;

// bool crosses the native boundary as one byte, exactly as in C++: the generated native functions in AnimationNative return bools,
// and Iris requires this attribute for them.
[assembly: DisableRuntimeMarshalling]
