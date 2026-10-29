#include "NativeCallHarness.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <typeindex>
#include <utility>

#include "ChangeBuffer.hpp"
#include "ComponentRegistry.hpp"
#include "EngineContext.hpp"
#include "FenrirScripting/WarnOnceRecord.hpp"

namespace Fenrir::Testing
{
    namespace
    {
        // One registered native function, as Call finds it: its type-and-method name, its thunk and the thunk's
        // signature.
        struct Registered
        {
            std::string native;
            void* thunk;
            std::type_index signature;
        };
    } // namespace

    struct NativeCallHarness::Impl
    {
        HeadlessApp headless;
        // Empty: a native records its changes as functions of the App, which need no registration to play back.
        Detail::ComponentRegistry registry;
        Detail::ChangeBuffer changes;
        // The extensions keep their states alive, which the registered thunks point at.
        std::vector<ScriptingExtension> extensions;
        std::vector<Registered> registered;
        // What the thunks build each call's context from while this harness is the active Scripting.
        Detail::EngineContext context;
        bool contextActive = false;
    };

    NativeCallHarness::NativeCallHarness() : NativeCallHarness(std::vector<ScriptingExtension>{})
    {
    }

    NativeCallHarness::NativeCallHarness(std::vector<ScriptingExtension> extensions) : m_impl(std::make_unique<Impl>())
    {
        m_impl->extensions = std::move(extensions);
        for (const ScriptingExtension& extension : m_impl->extensions)
        {
            for (const NativeFunction& function : extension.functions)
            {
                // A stateful function must get a state of its type, as Install checks before it registers.
                const auto& wanted = function.StateType();
                if (wanted.has_value() && wanted != extension.m_stateType)
                {
                    throw std::runtime_error(
                        "the native function '" + function.Name() + "' of extension '" + extension.name +
                        "' takes an extension state of type '" + wanted->name() + "', but the extension " +
                        (extension.m_stateType.has_value()
                             ? "carries one of type '" + std::string(extension.m_stateType->name()) + "'"
                             : "carries none"));
                }
                if (const auto result = function.Register(extension.m_state.get()); !result.Succeeded())
                {
                    throw std::runtime_error("the native function '" + function.Name() + "' of extension '" +
                                             extension.name + "' could not be registered: " + result.diagnostic);
                }
                m_impl->registered.push_back(Registered{std::string(NativeFunction::NativeNameFor(function.Name())),
                                                        function.Thunk(), function.Signature()});
            }
        }
        m_impl->context.app = &m_impl->headless.Get();
        m_impl->context.components = &m_impl->registry;
        m_impl->context.changes = &m_impl->changes;
        if (!Detail::ActivateEngineContext(&m_impl->context))
        {
            throw std::runtime_error("another native call harness or Scripting is already active in this process");
        }
        m_impl->contextActive = true;
    }

    NativeCallHarness::~NativeCallHarness()
    {
        if (m_impl != nullptr && m_impl->contextActive)
        {
            Detail::DeactivateEngineContext(&m_impl->context);
        }
    }

    App& NativeCallHarness::GetApp() noexcept
    {
        return m_impl->headless.Get();
    }

    const RecordingLogger& NativeCallHarness::Logger() const noexcept
    {
        return m_impl->headless.Logger();
    }

    void* NativeCallHarness::Thunk(std::string_view native, const std::type_info& signature) const
    {
        const auto found = std::find_if(m_impl->registered.begin(), m_impl->registered.end(),
                                        [&](const Registered& function) { return function.native == native; });
        if (found == m_impl->registered.end())
        {
            throw std::runtime_error("no native function named '" + std::string(native) +
                                     "' is registered with this harness");
        }
        if (std::type_index(signature) != found->signature)
        {
            throw std::runtime_error("the native function '" + std::string(native) + "' was called as '" +
                                     signature.name() + "', but its registered signature is '" +
                                     found->signature.name() + "'");
        }
        return found->thunk;
    }

    std::size_t NativeCallHarness::Warnings(std::string_view native, std::string_view message) const
    {
        const std::string line = WarnOnceRecord::Line(native, message);
        const auto messages = Logger().Messages();
        return static_cast<std::size_t>(
            std::count_if(messages.begin(), messages.end(), [&](const LoggedMessage& logged) {
                return logged.level == LogLevel::Warn && logged.text.ends_with(line);
            }));
    }

    CallContext NativeCallHarness::Context(std::string_view native) noexcept
    {
        return CallContext(m_impl->headless.Get(), m_impl->changes, native);
    }

    ApplyOutcome NativeCallHarness::ApplyChanges()
    {
        const Detail::ApplyResult result = m_impl->changes.Apply(m_impl->headless.Get(), m_impl->registry);
        return ApplyOutcome{result.applied, result.skipped};
    }

    std::size_t NativeCallHarness::PendingChanges() const noexcept
    {
        return m_impl->changes.Size();
    }
} // namespace Fenrir::Testing
