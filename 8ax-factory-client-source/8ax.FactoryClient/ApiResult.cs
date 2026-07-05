namespace EightAxis.FactoryClient;

internal sealed class ApiResult<T>
{
    public bool Success { get; }
    public T? Value { get; }
    public string Error { get; }

    private ApiResult(bool success, T? value, string error)
    {
        Success = success;
        Value = value;
        Error = error;
    }

    public static ApiResult<T> Ok(T value) => new(true, value, "");

    public static ApiResult<T> Fail(string error) => new(false, default, error);
}
