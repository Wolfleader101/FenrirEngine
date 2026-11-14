using System.Runtime.CompilerServices;

// bool crosses the native boundary as one byte, exactly as in C++: UINative's natives return bools, and Iris requires
// this attribute for them.
[assembly: DisableRuntimeMarshalling]
