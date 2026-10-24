namespace DescribeApi;

/// <summary>Something the tool cannot describe; the message is the diagnostic it prints.</summary>
internal sealed class DescribeException(string message) : Exception(message);
