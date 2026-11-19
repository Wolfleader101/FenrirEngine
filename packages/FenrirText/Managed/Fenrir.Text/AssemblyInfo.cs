using System.Runtime.CompilerServices;

// bool crosses the native boundary as one byte, exactly as in C++: most TextNative functions return a bool, and Iris
// requires this attribute for it.
[assembly: DisableRuntimeMarshalling]
