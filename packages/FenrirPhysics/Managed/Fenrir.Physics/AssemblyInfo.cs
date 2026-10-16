using System.Runtime.CompilerServices;

// bool crosses the native boundary as one byte, exactly as in C++: the native functions in PhysicsNative return bools,
// and Iris requires this attribute for them.
[assembly: DisableRuntimeMarshalling]
