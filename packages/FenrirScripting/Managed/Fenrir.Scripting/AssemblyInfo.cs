using System.Runtime.CompilerServices;

// bool and char cross the native boundary as one byte and two bytes, exactly as in C++. Iris requires this attribute
// for any native function (see Native.cs) that uses them.
[assembly: DisableRuntimeMarshalling]
