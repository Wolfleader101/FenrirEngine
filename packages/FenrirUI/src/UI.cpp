#include "FenrirUI/UI.hpp"

#include <algorithm>
#include <functional>
#include <utility>
#include <vector>

#include "FenrirApp/App.hpp"
#include "FenrirAssets/AssetStore.hpp"
#include "FenrirText/FontLibrary.hpp"
#include "FenrirUI/Controls.hpp"

namespace Fenrir
{
    UIRoots::UIRoots(App& app, AssetHandle<Shader> painterShader, const UIConfig& config)
        : settings(config.settings), m_app(app), m_painterShader(painterShader)
    {
        RegisterElement("panel", [] { return std::make_unique<Element>(); });
        RegisterElement("label", [] { return std::make_unique<Label>(); });
        RegisterElement("button", [] { return std::make_unique<Button>(); });
        RegisterElement("checkbox", [] { return std::make_unique<Checkbox>(); });
        RegisterElement("slider", [] { return std::make_unique<Slider>(); });
        RegisterElement("progress", [] { return std::make_unique<ProgressBar>(); });
        RegisterElement("image", [] { return std::make_unique<ImageView>(); });
        RegisterElement("scroll", [] { return std::make_unique<ScrollView>(); });
        RegisterElement("textentry", [] { return std::make_unique<TextEntry>(); });
    }

    UIRoots::~UIRoots() = default;

    UIRoot& UIRoots::CreateRoot(UIRootConfig config)
    {
        RootEntry entry;
        entry.root = std::make_unique<UIRoot>(m_app, *this, std::move(config), UIRootId{m_nextId++});
        entry.order = m_nextOrder++;
        UIRoot& root = *entry.root;
        // Kept in drawing order: by layer, then as made.
        const auto at =
            std::upper_bound(m_roots.begin(), m_roots.end(), entry, [](const RootEntry& a, const RootEntry& b) {
                const std::int32_t layerA = a.root->GetConfig().layer;
                const std::int32_t layerB = b.root->GetConfig().layer;
                return layerA != layerB ? layerA < layerB : a.order < b.order;
            });
        m_roots.insert(at, std::move(entry));
        return root;
    }

    bool UIRoots::DestroyRoot(const UIRoot& root)
    {
        const auto it = std::find_if(m_roots.begin(), m_roots.end(),
                                     [&](const RootEntry& entry) { return entry.root.get() == &root; });
        if (it == m_roots.end())
        {
            return false;
        }
        it->root->Retire();
        m_retired.push_back(std::move(it->root));
        m_roots.erase(it);
        return true;
    }

    std::vector<UIRoot*> UIRoots::GetRoots() const
    {
        std::vector<UIRoot*> roots;
        roots.reserve(m_roots.size());
        for (const RootEntry& entry : m_roots)
        {
            roots.push_back(entry.root.get());
        }
        return roots;
    }

    void UIRoots::RegisterElement(std::string_view tag, ElementFactory factory)
    {
        m_factories.insert_or_assign(std::string(tag), std::move(factory));
    }

    bool UIRoots::IsRegistered(std::string_view tag) const
    {
        const auto it = m_factories.find(std::string(tag));
        return it != m_factories.end() && it->second;
    }

    std::unique_ptr<Element> UIRoots::MakeElement(std::string_view tag) const
    {
        const auto it = m_factories.find(std::string(tag));
        if (it != m_factories.end() && it->second)
        {
            if (std::unique_ptr<Element> element = it->second())
            {
                return element;
            }
            m_app.Logger()->Warn("FenrirUI: the factory registered for <{}> made no element; it is a plain element",
                                 tag);
        }
        return std::make_unique<Element>();
    }

    FontLibrary* UIRoots::GetFonts() const noexcept
    {
        return m_app.TryGetResource<FontLibrary>();
    }

    bool UIRoots::AddFontFace(const UIFontFace& face)
    {
        FontLibrary* fonts = GetFonts();
        if (fonts == nullptr || face.font.IsEmpty())
        {
            return false;
        }
        fonts->AddFace(face.family, face.font, face.weight, face.style, face.rendering);
        const auto same = std::find_if(m_fontFaces.begin(), m_fontFaces.end(), [&](const UIFontFace& registered) {
            return registered.family == face.family && registered.weight == face.weight &&
                   registered.style == face.style;
        });
        if (same != m_fontFaces.end())
        {
            *same = face;
        }
        else
        {
            m_fontFaces.push_back(face);
        }
        return true;
    }

    std::span<const UIFontFace> UIRoots::GetFontFaces() const noexcept
    {
        return m_fontFaces;
    }

    namespace
    {
        // Adds `work` after every entry of a lower or equal order: equal orders run in the order added.
        void AddStageWork(std::vector<Detail::StageWork>& stage, std::uint64_t id, std::int32_t order,
                          std::function<void()> work)
        {
            const auto at = std::upper_bound(
                stage.begin(), stage.end(), order,
                [](std::int32_t value, const Detail::StageWork& entry) { return value < entry.order; });
            stage.insert(at, Detail::StageWork{id, order, std::move(work)});
        }

        bool RemoveStageWork(std::vector<Detail::StageWork>& stage, std::uint64_t id)
        {
            const auto found = std::ranges::find(stage, id, &Detail::StageWork::id);
            if (id == 0 || found == stage.end())
            {
                return false;
            }
            stage.erase(found);
            return true;
        }

        // Work added or removed while the stage runs takes effect next frame.
        void RunStage(const std::vector<Detail::StageWork>& stage)
        {
            const std::vector<Detail::StageWork> running = stage;
            for (const Detail::StageWork& entry : running)
            {
                entry.work();
            }
        }
    } // namespace

    BuildStageHandle UIRoots::AddBuildStage(std::int32_t order, std::function<void()> work)
    {
        const BuildStageHandle handle{m_nextBuildStage++};
        AddStageWork(m_buildStage, handle.id, order, std::move(work));
        return handle;
    }

    bool UIRoots::RemoveBuildStage(BuildStageHandle handle)
    {
        return RemoveStageWork(m_buildStage, handle.id);
    }

    PaintStageHandle UIRoots::AddPaintStage(std::int32_t order, std::function<void()> work)
    {
        const PaintStageHandle handle{m_nextPaintStage++};
        AddStageWork(m_paintStage, handle.id, order, std::move(work));
        return handle;
    }

    bool UIRoots::RemovePaintStage(PaintStageHandle handle)
    {
        return RemoveStageWork(m_paintStage, handle.id);
    }

    void UIRoots::Frame()
    {
        m_retired.clear();
        // First, before any root advances its transitions or resolves its styles: what the build stage makes and
        // changes is laid out and painted this frame.
        RunStage(m_buildStage);
        std::vector<UIRoot*> laidOut;
        for (const RootEntry& entry : m_roots)
        {
            if (entry.root->LayOut())
            {
                laidOut.push_back(entry.root.get());
            }
        }
        RunStage(m_paintStage);
        for (UIRoot* root : laidOut)
        {
            // A root the stage destroyed paints no more.
            const bool alive =
                std::ranges::any_of(m_roots, [&](const RootEntry& entry) { return entry.root.get() == root; });
            if (alive)
            {
                root->PaintAndSubmit();
            }
        }
    }

    UI UI::Install(App& app, UIConfig config)
    {
        if (app.TryGetResource<UIRoots>() != nullptr)
        {
            app.Logger()->Error("FenrirUI: the app already has a UI installed");
            return UI(nullptr, std::nullopt, false);
        }
        Painting painting = Painting::Install(app);
        if (!painting.IsValid())
        {
            // Painting::Install logged why.
            return UI(nullptr, std::move(painting), false);
        }
        // Painting::Install succeeds only over an asset store, which the font library uses.
        const bool ownsFonts = app.TryGetResource<FontLibrary>() == nullptr;
        if (ownsFonts)
        {
            app.AddResource<FontLibrary>(app.GetResource<AssetStore>());
        }
        app.AddResource<UIRoots>(app, painting.GetShader(), config);
        app.AddSequentialSystem(SchedulePriority::PostUpdate, [](App& running) {
            if (UIRoots* roots = running.TryGetResource<UIRoots>())
            {
                roots->Frame();
            }
        });
        return UI(&app, std::move(painting), ownsFonts);
    }

    UI::UI(App* app, std::optional<Painting> painting, bool ownsFonts) noexcept
        : m_app(app), m_painting(std::move(painting)), m_ownsFonts(ownsFonts)
    {
    }

    UI::UI(UI&& other) noexcept
        : m_app(std::exchange(other.m_app, nullptr)), m_painting(std::move(other.m_painting)),
          m_ownsFonts(std::exchange(other.m_ownsFonts, false))
    {
    }

    UI& UI::operator=(UI&& other) noexcept
    {
        if (this != &other)
        {
            Uninstall();
            m_app = std::exchange(other.m_app, nullptr);
            m_painting = std::move(other.m_painting);
            m_ownsFonts = std::exchange(other.m_ownsFonts, false);
        }
        return *this;
    }

    UI::~UI()
    {
        Uninstall();
    }

    void UI::Uninstall() noexcept
    {
        if (m_app == nullptr)
        {
            return;
        }
        // The roots' paragraphs go before the library whose atlases they name.
        m_app->RemoveResource<UIRoots>();
        if (m_ownsFonts)
        {
            m_app->RemoveResource<FontLibrary>();
        }
    }
} // namespace Fenrir
