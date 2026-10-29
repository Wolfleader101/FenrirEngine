#pragma once

// The rules tests' adapter at the extension set's registrar seam: it registers nothing with Iris, refuses the one
// binding name it is told to, and records every registration in order with the state it was given, so each rule of the
// extension set is checked with no .NET.
#include "NativeRegistrar.hpp"

#include <string>
#include <vector>

namespace RulesTest
{
    class FakeNativeRegistrar final : public Fenrir::Detail::NativeRegistrar
    {
      public:
        // What a case tells the fake to do: the binding name whose registration it refuses (none when empty).
        std::string refuse;

        // What the set asked of it: every registration in order, refused ones included.
        struct Registered
        {
            std::string bindingName;
            std::string nativeName;
            void* state;
        };
        std::vector<Registered> registered;

        Iris::CallResult<void> Register(const Fenrir::NativeFunction& function, void* state) override
        {
            registered.push_back(
                {function.BindingName(), std::string(Fenrir::NativeFunction::NativeNameFor(function.Name())), state});
            if (!refuse.empty() && function.BindingName() == refuse)
            {
                return {Iris::CallStatus::InvalidArgument, "the fake registrar refused " + refuse, false};
            }
            return {Iris::CallStatus::Success, {}, false};
        }

        [[nodiscard]] std::vector<std::string> BindingNames() const
        {
            std::vector<std::string> names;
            for (const auto& entry : registered)
            {
                names.push_back(entry.bindingName);
            }
            return names;
        }
    };
} // namespace RulesTest
