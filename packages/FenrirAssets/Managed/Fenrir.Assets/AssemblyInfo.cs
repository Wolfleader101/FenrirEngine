using System.Runtime.CompilerServices;

// bool crosses the native boundary as one byte, exactly as in C++: the native functions in AssetsNative (Generated/) take
// and return bools, and Iris requires this attribute for them.
[assembly: DisableRuntimeMarshalling]
