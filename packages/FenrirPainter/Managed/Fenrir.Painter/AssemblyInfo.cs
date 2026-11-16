using System.Runtime.CompilerServices;

// bool crosses the native boundary as one byte, exactly as in C++: PainterNative.Release returns a bool, and Iris
// requires this attribute for it.
[assembly: DisableRuntimeMarshalling]
