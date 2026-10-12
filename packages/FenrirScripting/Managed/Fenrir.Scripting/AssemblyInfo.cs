using System.Runtime.CompilerServices;

// bool and char cross the native boundary as one byte and two bytes, exactly as in C++. Iris requires this attribute
// for any internal call (see Native.cs) that uses them.
[assembly: DisableRuntimeMarshalling]
