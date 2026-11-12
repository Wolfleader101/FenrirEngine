#include "FenrirUI/UI.hpp"

#include <algorithm>
#include <utility>

#include "FenrirApp/App.hpp"
#include "FenrirAssets/AssetStore.hpp"
#include "FenrirText/FontLibrary.hpp"

namespace Fenrir
{
    UIRoots::UIRoots(App& app, AssetHandle<Shader> painterShader, const UIConfig& config)
        : settings(config.settings), m_app(app), m_painterShader(painterShader)
    {
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

    void UIRoots::Frame()
    {
        m_retired.clear();
        for (const RootEntry& entry : m_roots)
        {
            entry.root->Frame();
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
