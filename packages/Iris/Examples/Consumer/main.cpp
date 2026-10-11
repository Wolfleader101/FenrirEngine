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
            if (isScript.Succeeded() && *isScript)
            {
                std::cout << "Script type: " << type.GetInfo()->fullName << '\n';
                ++scripts;
            }
        }

        // Objects: construct one, then call an instance method found once and callable many times.
        const auto counterType = component.FindType(std::string(ns) + "Counter");
        if (!Ok(counterType, "FindType(Counter)"))
            return false;
        const auto next = counterType->FindInstanceMethod<std::int32_t()>("Next");
        auto counter = counterType->CreateObject(std::int32_t{41});
        if (!Ok(next, "FindInstanceMethod(Next)") || !Ok(counter, "CreateObject"))
            return false;
        const auto counted = next->Invoke(*counter);

        // Strings, structs and arrays as arguments and returns.
        const auto strings = component.FindType(std::string(ns) + "Strings");
        const auto echo = strings->FindMethod<std::optional<std::string>(std::string_view)>("Echo");
        const auto echoed = echo->Invoke(text);

        const auto structs = component.FindType(std::string(ns) + "Structs");
        const auto add = structs->FindMethod<Vec3(Vec3, Vec3)>("Add");
        if (!Ok(add, "FindMethod(Structs.Add)"))
            return false;
        const auto sum = add->Invoke(Vec3{1, 2, 3}, Vec3{10, 20, 30});

        const auto arrays = component.FindType(std::string(ns) + "Arrays");
        const auto range = arrays->FindMethod<std::optional<Array<std::int32_t>>(std::int32_t)>("Range");
        const auto total = arrays->FindMethod<std::int32_t(const Array<std::int32_t>&)>("Sum");
        auto numbers = range->Invoke(5); // 0, 1, 2, 3, 4
        if (!Ok(numbers, "Arrays.Range") || !Expect(numbers->has_value(), "an array, not null") ||
            !Ok(total, "FindMethod(Arrays.Sum)"))
            return false;
        const auto summed = total->Invoke(**numbers);

        // Managed code calling a registered native function (Natives.Add, bound when the component loaded).
        const auto natives = component.FindType(std::string(ns) + "Natives");
        const auto callAdd = natives->FindMethod<std::int32_t(std::int32_t, std::int32_t)>("CallAdd");
        const auto viaNative = callAdd->Invoke(2, 3);

        std::cout << "Script types: " << scripts << ", Counter(41).Next() = " << counted.value.value_or(-1)
                  << ", Echo = " << (echoed.Succeeded() && *echoed ? **echoed : std::string("<failed or null>"))
                  << ", Vec3 sum X = " << (sum.Succeeded() ? sum->X : -1.0f)
                  << ", Sum(Range(5)) = " << summed.value.value_or(-1)
                  << ", native Add(2, 3) = " << viaNative.value.value_or(-1) << '\n';

        // Release what we own before the component: live objects and arrays make Component::Release refuse.
        const bool released = (*numbers)->Release().Succeeded() && counter->Release().Succeeded();
        return Expect(scripts == 4, "four script types") && Ok(counted, "Counter.Next") &&
               Expect(*counted == 42, "42") && Ok(echoed, "Strings.Echo") &&
               Expect(*echoed == text, "the echoed string") && Ok(sum, "Structs.Add") &&
               Expect(sum->X == 11 && sum->Z == 33, "the Vec3 sum") && Ok(summed, "Arrays.Sum") &&
               Expect(*summed == 10, "10") && Ok(viaNative, "Natives.CallAdd") && Expect(*viaNative == 5, "5") &&
               Expect(released, "objects released");
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
