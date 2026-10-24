// A small script host built on an installed Iris package, the way any C++ project would use it: discover the
// component's script types, create an object and call it, pass strings, structs and arrays, and let managed
// code call back into native functions.
// Usage: IrisConsumer component.dll
#include "../Hosting/ExampleNatives.hpp"
#include <Iris/Iris.hpp>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

// Mirrors the component's `struct Vec3 { public float X, Y, Z; }`; Iris checks the layout at lookup.
struct Vec3
{
    float X, Y, Z;
};

template <>
struct Iris::StructMapping<Vec3>
{
    static constexpr std::string_view typeName = "Iris.Examples.Component.Vec3";
    static constexpr auto fields = Iris::StructFields(IRIS_FIELD(Vec3, X), IRIS_FIELD(Vec3, Y), IRIS_FIELD(Vec3, Z));
};

namespace
{
    // Reports a failed step. Every Iris call returns a result; nothing here throws.
    template <typename T>
    bool Ok(const Iris::CallResult<T>& result, std::string_view step)
    {
        if (!result.Succeeded())
        {
            std::cerr << step << " failed: " << result.diagnostic << '\n';
        }
        return result.Succeeded();
    }

    bool Expect(bool condition, std::string_view what)
    {
        if (!condition)
        {
            std::cerr << "Unexpected result: " << what << '\n';
        }
        return condition;
    }

    // Finds `Iris.Examples.Component.<type>.<method>` with the given signature, reporting whichever step failed.
    template <typename Signature>
    auto FindStatic(const Iris::Component& component, std::string_view type, std::string_view method)
        -> decltype(std::declval<const Iris::Type&>().FindMethod<Signature>(method).value)
    {
        const std::string typeName = "Iris.Examples.Component." + std::string(type);
        const auto found = component.FindType(typeName);
        if (!Ok(found, "FindType(" + typeName + ")"))
            return std::nullopt;
        auto handle = found->template FindMethod<Signature>(method);
        if (!Ok(handle, "FindMethod(" + std::string(method) + ")"))
            return std::nullopt;
        return std::move(handle.value);
    }

    bool Run(const Iris::Component& component)
    {
        using namespace Iris;
        constexpr std::string_view ns = "Iris.Examples.Component.";
        constexpr std::string_view text = "h\xC3\xA9llo"; // UTF-8 bytes spelled out, whatever the source encoding

        // Discover scripts: the component's public types that implement the shared contract.
        const auto types = component.GetTypes();
        if (!Ok(types, "GetTypes"))
            return false;
        int scripts = 0;
        for (const Type& type : *types)
        {
            const auto isScript = type.IsAssignableTo("Iris.Examples.Contracts.IScript");
            if (!isScript.Succeeded() || !*isScript)
                continue;
            const auto info = type.GetInfo();
            if (!Ok(info, "Type::GetInfo"))
                return false;
            std::cout << "Script type: " << info->fullName << '\n';
            ++scripts;
        }
        if (!Expect(scripts == 4, "four script types"))
            return false;

        // Objects: construct one, then call an instance method found once and callable many times.
        const auto counterType = component.FindType(std::string(ns) + "Counter");
        if (!Ok(counterType, "FindType(Counter)"))
            return false;
        const auto next = counterType->FindInstanceMethod<std::int32_t()>("Next");
        auto counter = counterType->CreateObject(std::int32_t{41});
        if (!Ok(next, "FindInstanceMethod(Next)") || !Ok(counter, "CreateObject"))
            return false;
        const auto counted = next->Invoke(*counter);
        if (!Ok(counted, "Counter.Next") || !Expect(*counted == 42, "Counter(41).Next() == 42"))
            return false;

        // A string in and out.
        const auto echo = FindStatic<std::optional<std::string>(std::string_view)>(component, "Strings", "Echo");
        if (!echo)
            return false;
        const auto echoed = echo->Invoke(text);
        if (!Ok(echoed, "Strings.Echo") || !Expect(*echoed == text, "the echoed string"))
            return false;

        // A mapped struct by value, in and out.
        const auto add = FindStatic<Vec3(Vec3, Vec3)>(component, "Structs", "Add");
        if (!add)
            return false;
        const auto sum = add->Invoke(Vec3{1, 2, 3}, Vec3{10, 20, 30});
        if (!Ok(sum, "Structs.Add") || !Expect(sum->X == 11 && sum->Z == 33, "the Vec3 sum"))
            return false;

        // An array returned from one method and passed to another.
        const auto range = FindStatic<std::optional<Array<std::int32_t>>(std::int32_t)>(component, "Arrays", "Range");
        const auto total = FindStatic<std::int32_t(const Array<std::int32_t>&)>(component, "Arrays", "Sum");
        if (!range || !total)
            return false;
        auto numbers = range->Invoke(5); // 0, 1, 2, 3, 4
        if (!Ok(numbers, "Arrays.Range") || !Expect(numbers->has_value(), "an array, not null"))
            return false;
        const auto summed = total->Invoke(**numbers);
        if (!Ok(summed, "Arrays.Sum") || !Expect(*summed == 10, "Sum(Range(5)) == 10"))
            return false;

        // Managed code calling a registered native function (Natives.Add, bound when the component loaded).
        const auto callAdd = FindStatic<std::int32_t(std::int32_t, std::int32_t)>(component, "Natives", "CallAdd");
        if (!callAdd)
            return false;
        const auto viaNative = callAdd->Invoke(2, 3);
        if (!Ok(viaNative, "Natives.CallAdd") || !Expect(*viaNative == 5, "native Add(2, 3) == 5"))
            return false;

        std::cout << "Script types: " << scripts << ", Counter(41).Next() = " << *counted << ", Echo ok"
                  << ", Vec3 sum X = " << sum->X << ", Sum(Range(5)) = " << *summed
                  << ", native Add(2, 3) = " << *viaNative << '\n';

        // Release what we own before the component: live objects and arrays make Component::Release refuse.
        return Ok((*numbers)->Release(), "Array::Release") && Ok(counter->Release(), "Object::Release");
    }
} // namespace

#if defined(_WIN32)
int wmain(int argc, wchar_t* argv[])
#else
int main(int argc, char* argv[])
#endif
{
    if (argc != 2)
    {
        std::cerr << "Usage: IrisConsumer component.dll\n";
        return EXIT_FAILURE;
    }
    // iris_deploy put the bridge and its runtime configuration next to this executable.
    const auto here = std::filesystem::absolute(argv[0]).parent_path();

    // The component calls back into native code; register those functions before loading it.
    if (const auto natives = Iris::Examples::RegisterNatives(); !Ok(natives, "Registering native functions"))
    {
        return EXIT_FAILURE;
    }

    Iris::Host host;
    const auto initialized = host.Initialize(
        {.runtimeConfigPath = here / "Iris.Bridge.runtimeconfig.json", .bridgeAssemblyPath = here / "Iris.Bridge.dll"});
    if (!initialized.Succeeded())
    {
        std::cerr << "Initialize failed: " << initialized.diagnostic << '\n';
        return EXIT_FAILURE;
    }
    // Warnings and errors from the bridge (an exception in component code, a type that failed to load).
    (void)host.SetLogSink([](const Iris::LogMessage& message) {
        if (message.level >= Iris::LogLevel::Warning)
        {
            std::cerr << "[iris] " << message.text << '\n';
        }
    });

    auto loaded = host.LoadComponent(argv[1]);
    if (!Ok(loaded, "LoadComponent"))
    {
        return EXIT_FAILURE;
    }
    Iris::Component component = *std::move(loaded);
    const bool ran = Run(component);
    const bool released = Ok(component.Release(), "Component::Release");
    (void)host.SetLogSink({});
    std::cout << "Iris consumer: " << (ran && released ? "ok" : "failed") << '\n';
    return ran && released ? EXIT_SUCCESS : EXIT_FAILURE;
}
