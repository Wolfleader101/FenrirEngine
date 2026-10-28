#include "GenerationLifecycle.hpp"

#include <algorithm>
#include <exception>
#include <fstream>
#include <iterator>
#include <regex>
#include <string>
#include <system_error>
#include <utility>

namespace Fenrir::Detail
{
    // It is deleted when the last owner lets go: the generation loaded from it, a released generation that has not
    // unloaded yet (Windows will not delete a loaded assembly), or the "previous" slot that keeps it in case the next
    // build fails to load.
    struct ScriptCopy
    {
        explicit ScriptCopy(std::filesystem::path path) : directory(std::move(path))
        {
        }
        ~ScriptCopy()
        {
            std::error_code ignored;
            std::filesystem::remove_all(directory, ignored);
        }
        ScriptCopy(const ScriptCopy&) = delete;
        ScriptCopy& operator=(const ScriptCopy&) = delete;

        std::filesystem::path directory;
    };

    namespace
    {
        using Result = GenerationLifecycle::Result;

        // Whether the file can be opened for reading right now: a build still writing it, or a virus scanner holding
        // it, says no.
        bool CanBeRead(const std::filesystem::path& path)
        {
            std::ifstream stream(path, std::ios::binary);
            return stream.good();
        }

        // Copies the script assembly and what it needs into a new directory under `root`: the files named like it (its
        // pdb and deps.json), and every dll its deps.json lists that sits beside it. A build can then overwrite the
        // original while the copy is loaded. Returns null and says why in `error` if it could not.
        std::shared_ptr<ScriptCopy> MakeCopy(const std::filesystem::path& assembly, const std::filesystem::path& root,
                                             int number, std::string& error)
        {
            std::error_code code;
            const auto directory = root / ("gen-" + std::to_string(number));
            std::filesystem::remove_all(directory, code);
            std::filesystem::create_directories(directory, code);
            if (code)
            {
                error = "The directory '" + directory.string() + "' could not be made: " + code.message();
                return nullptr;
            }
            auto copy = std::make_shared<ScriptCopy>(directory);
            const auto source = assembly.parent_path();
            // Native strings throughout: a file beside the assembly may have a name the narrow code page cannot hold.
            const std::filesystem::path::string_type prefix =
                assembly.stem().native() + std::filesystem::path::string_type(1, '.');

            std::vector<std::filesystem::path> files{assembly};
            for (const auto& entry : std::filesystem::directory_iterator(source, code))
            {
                // A copy, not a reference: filename() is a temporary, and a reference to its string would dangle.
                const auto name = entry.path().filename().native();
                if (entry.is_regular_file(code) && entry.path() != assembly && name.rfind(prefix, 0) == 0)
                {
                    files.push_back(entry.path());
                }
            }
            auto dependencies = source / assembly.stem();
            dependencies += ".deps.json";
            if (std::ifstream stream(dependencies); stream.good())
            {
                const std::string text((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
                static const std::regex dll(R"(([A-Za-z0-9_.+\-]+\.dll)\")");
                for (std::sregex_iterator found(text.begin(), text.end(), dll), end; found != end; ++found)
                {
                    const auto path = source / (*found)[1].str();
                    if (std::filesystem::is_regular_file(path, code) &&
                        std::find(files.begin(), files.end(), path) == files.end())
                    {
                        files.push_back(path);
                    }
                }
            }
            for (const auto& file : files)
            {
                std::filesystem::copy_file(file, directory / file.filename(),
                                           std::filesystem::copy_options::overwrite_existing, code);
                if (code)
                {
                    error = "The file '" + file.string() + "' could not be copied: " + code.message();
                    return nullptr;
                }
            }
            return copy;
        }
    } // namespace

    void GenerationLifecycle::Configure(ScriptAssemblyLoader& loader, ILogger* logger, Settings settings, Hooks hooks)
    {
        m_loader = &loader;
        m_logger = logger;
        m_settings = std::move(settings);
        m_hooks = std::move(hooks);
        m_watcher = ReloadWatcher({m_settings.hotReload, m_settings.reloadInterval, m_settings.reloadDebounce});
    }

    std::optional<GenerationLifecycle::FileStamp> GenerationLifecycle::StampNow() const
    {
        std::error_code error;
        const auto modified = std::filesystem::last_write_time(m_settings.assembly, error);
        if (error)
        {
            return std::nullopt;
        }
        const auto size = std::filesystem::file_size(m_settings.assembly, error);
        if (error)
        {
            return std::nullopt;
        }
        return FileStamp{modified, size};
    }

    GenerationLifecycle::Result GenerationLifecycle::Load()
    {
        if (m_loader == nullptr)
        {
            return Result{Iris::CallStatus::NotInitialized, "Scripting has no script assembly to load.", false};
        }
        auto loaded = LoadGeneration(nullptr);
        if (loaded.Succeeded())
        {
            m_watcher.MarkLoaded(StampNow());
        }
        return loaded;
    }

    GenerationLifecycle::Result GenerationLifecycle::LoadGeneration(std::shared_ptr<ScriptCopy> existing)
    {
        const auto& assembly = m_settings.assembly;
        std::shared_ptr<ScriptCopy> copy = std::move(existing);
        if (copy == nullptr)
        {
            std::string error;
            copy = MakeCopy(assembly, m_settings.scratchRoot, ++m_copyCounter, error);
            if (copy == nullptr)
            {
                return Result{
                    Iris::CallStatus::InvalidArgument,
                    "The script assembly '" + assembly.string() + "' could not be copied for loading. " + error, false};
            }
        }
        auto loaded = m_loader->Load(copy->directory / assembly.filename());
        if (!loaded.Succeeded())
        {
            return Result{loaded.status,
                          "The script assembly '" + assembly.string() + "' could not be loaded: " + loaded.diagnostic,
                          loaded.diagnosticTruncated};
        }
        // All or nothing: the assembly stays loaded only if the host accepts what it defines.
        if (auto validated = m_hooks.reconcile(**loaded.value); !validated.Succeeded())
        {
            Retire(std::move(*loaded.value), copy);
            validated.diagnostic =
                "The script assembly '" + assembly.string() + "' was released. " + validated.diagnostic;
            return validated;
        }
        m_component = std::move(*loaded.value);
        m_activeCopy = copy;
        ++m_generation;
        if (auto started = m_hooks.start(*m_component); !started.Succeeded())
        {
            started.diagnostic = "The script assembly '" + assembly.string() + "' was released. " + started.diagnostic;
            Unload();
            return started;
        }
        return Result{Iris::CallStatus::Success, {}, false};
    }

    void GenerationLifecycle::Retire(std::unique_ptr<LoadedScriptAssembly> released, std::shared_ptr<ScriptCopy> copy)
    {
        (void)released->Release();
        m_retired.push_back({std::move(released), std::move(copy), m_generation, false});
    }

    void GenerationLifecycle::Unload()
    {
        if (m_hooks.stop)
        {
            m_hooks.stop();
        }
        if (m_component != nullptr)
        {
            Retire(std::move(m_component), m_activeCopy);
            m_previousCopy = m_activeCopy;
            m_activeCopy.reset();
        }
    }

    void GenerationLifecycle::Sweep()
    {
        for (auto at = m_retired.begin(); at != m_retired.end();)
        {
            const auto unloaded = at->component->WaitForUnload(m_settings.unloadTimeout);
            if (unloaded.Succeeded() && *unloaded.value == Iris::ComponentUnloadState::Collected)
            {
                at = m_retired.erase(at);
                continue;
            }
            if (!at->warned && m_logger != nullptr)
            {
                m_logger->Warn(
                    "Scripting: generation {} of the script assembly has not unloaded; something still holds a type "
                    "from "
                    "it (a thread it started, or a static that keeps one). Its copy stays on disk until it does.",
                    at->generation);
            }
            at->warned = true;
            ++at;
        }
    }

    GenerationLifecycle::Result GenerationLifecycle::Reload()
    {
        if (m_reloading || m_loader == nullptr || m_settings.assembly.empty())
        {
            return Result{Iris::CallStatus::NotInitialized,
                          "Scripting is not running a script assembly that can be reloaded.", false};
        }
        m_reloading = true;
        struct Done
        {
            bool& flag;
            ~Done()
            {
                flag = false;
            }
        } done{m_reloading};

        const auto name = m_settings.assembly.filename().string();
        m_logger->Info("Scripting: reloading '{}'.", name);
        // Anything a script left recorded is played back first, so no entity is left half made.
        if (m_hooks.beforeReload)
        {
            m_hooks.beforeReload();
        }
        m_watcher.MarkLoaded(StampNow());

        Unload();
        // What to go back to if the new build does not load. Taken now: a generation that loads and then fails to start
        // unloads itself, and would otherwise replace it with its own copy.
        const std::shared_ptr<ScriptCopy> restoreCopy = m_previousCopy;
        Sweep();
        const auto fresh = LoadGeneration(nullptr);
        if (fresh.Succeeded())
        {
            m_previousCopy.reset();
            m_logger->Info("Scripting: reloaded '{}' (generation {}).", name, m_generation);
            return fresh;
        }
        m_logger->Error("Scripting: the new build of '{}' did not load. {}", name, fresh.diagnostic);
        if (restoreCopy == nullptr)
        {
            return fresh;
        }
        auto restored = LoadGeneration(restoreCopy);
        if (restored.Succeeded())
        {
            m_logger->Warn("Scripting: the previous build of '{}' is running again.", name);
        }
        else
        {
            m_logger->Error("Scripting: the previous build could not be restored either; no scripts are running. {}",
                            restored.diagnostic);
        }
        return fresh;
    }

    GenerationLifecycle::Watch GenerationLifecycle::Poll(std::chrono::steady_clock::time_point now)
    {
        if (m_reloading || m_loader == nullptr)
        {
            return Watch::Idle;
        }
        return m_watcher.Poll(now, [this] { return StampNow(); }, [this] { return CanBeRead(m_settings.assembly); });
    }

    void GenerationLifecycle::ReloadIfReady(std::chrono::steady_clock::time_point now) noexcept
    {
        try
        {
            if (Poll(now) == Watch::Ready)
            {
                (void)Reload();
            }
        }
        catch (const std::exception& exception)
        {
            m_reloading = false;
            if (m_logger != nullptr)
            {
                m_logger->Error("Scripting: reloading failed: {}", exception.what());
            }
        }
        catch (...)
        {
            m_reloading = false;
        }
    }

    void GenerationLifecycle::Release() noexcept
    {
        m_retired.clear();
        m_previousCopy.reset();
        m_activeCopy.reset();
        if (!m_settings.scratchRoot.empty())
        {
            std::error_code ignored;
            // Only succeeds when it is empty: leaves other work alone.
            std::filesystem::remove(m_settings.scratchRoot, ignored);
        }
    }
} // namespace Fenrir::Detail
