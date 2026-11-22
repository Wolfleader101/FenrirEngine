namespace GenerateTypeRegistry;

/// <summary>Why a registration could not be generated: the message names the file, type or field at fault.</summary>
internal sealed class GenerateException(string message) : Exception(message);
