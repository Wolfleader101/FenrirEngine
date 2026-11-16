using System.Reflection;
using Microsoft.AspNetCore.Components;

namespace Fenrir.UI.Razor;

/// <summary>
/// Sets a child Razor component's parameters by name (spec: "The builder" child components, ADR-0067). The engine's
/// source generators cannot see a <c>.razor</c> file's <c>@code</c> members, so each type's <c>[Parameter]</c>
/// properties are found by reflection once, their setters bound to typed delegates, and the result kept per type until
/// <see cref="Release"/>. Names match as Blazor matches them, ignoring case.
/// </summary>
internal static class ComponentParameters
{
    private static readonly Dictionary<Type, Dictionary<string, Action<object, object?>>> setters = [];

    private static readonly MethodInfo BindMethod =
        typeof(ComponentParameters).GetMethod(nameof(Bind), BindingFlags.NonPublic | BindingFlags.Static)!;

    /// <summary>Sets each of <paramref name="parameters"/> on <paramref name="component"/>, in order.</summary>
    public static void Set(UIComponent component, List<KeyValuePair<string, object?>> parameters)
    {
        Type type = component.GetType();
        Dictionary<string, Action<object, object?>> forType = SettersOf(type);
        foreach ((string name, object? value) in parameters)
        {
            if (!forType.TryGetValue(name, out Action<object, object?>? setter))
            {
                throw new InvalidOperationException(
                    $"The Razor component {type.FullName} has no [Parameter] property named {name}");
            }
            try
            {
                setter(component, value);
            }
            catch (Exception exception) when (exception is InvalidCastException or NullReferenceException)
            {
                throw new InvalidOperationException(
                    $"The parameter {name} of the Razor component {type.FullName} cannot take " +
                    $"{(value == null ? "null" : $"a {value.GetType().FullName}")}", exception);
            }
        }
    }

    /// <summary>Forgets every type's setters, which name a script generation's types.</summary>
    public static void Release() => setters.Clear();

    private static Dictionary<string, Action<object, object?>> SettersOf(Type type)
    {
        if (setters.TryGetValue(type, out Dictionary<string, Action<object, object?>>? found))
            return found;
        var made = new Dictionary<string, Action<object, object?>>(StringComparer.OrdinalIgnoreCase);
        // The most derived first, so a property hiding an inherited one of the same name wins.
        for (Type? level = type; level != null && level != typeof(object); level = level.BaseType)
        {
            foreach (PropertyInfo property in level.GetProperties(BindingFlags.Instance | BindingFlags.Public |
                                                                  BindingFlags.DeclaredOnly))
            {
                if (property.SetMethod is not { } setter || !property.IsDefined(typeof(ParameterAttribute), true))
                    continue;
                if (made.ContainsKey(property.Name))
                    continue;
                MethodInfo bind = BindMethod.MakeGenericMethod(level, property.PropertyType);
                made.Add(property.Name, (Action<object, object?>)bind.Invoke(null, [setter])!);
            }
        }
        setters.Add(type, made);
        return made;
    }

    private static Action<object, object?> Bind<TComponent, TValue>(MethodInfo setter)
    {
        var typed = setter.CreateDelegate<Action<TComponent, TValue>>();
        return (component, value) => typed((TComponent)component, (TValue)value!);
    }
}
