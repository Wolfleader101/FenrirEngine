#include "NativeCallHarness.hpp"

#include "ChangeBuffer.hpp"
#include "ComponentRegistry.hpp"

namespace Fenrir::Testing
{
    struct NativeCallHarness::Impl
    {
        HeadlessApp headless;
        // Empty: a native records its changes as functions of the App, which need no registration to play back.
        Detail::ComponentRegistry registry;
        Detail::ChangeBuffer changes;
    };

    NativeCallHarness::NativeCallHarness() : m_impl(std::make_unique<Impl>())
    {
    }

    NativeCallHarness::~NativeCallHarness() = default;

    App& NativeCallHarness::GetApp() noexcept
    {
        return m_impl->headless.Get();
    }

    const RecordingLogger& NativeCallHarness::Logger() const noexcept
    {
        return m_impl->headless.Logger();
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
