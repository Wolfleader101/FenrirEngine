#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <format>
#include <memory>
#include <string>

#include "Compass.hpp"
#include "FenrirApp/App.hpp"
#include "FenrirAssets/AssetStore.hpp"
#include "FenrirUI/Controls.hpp"
#include "FenrirUI/Markup.hpp"
#include "FenrirUI/UI.hpp"
#include "FenrirUI/UIRoot.hpp"
#include "UIDemo.hpp"

namespace UIDemo
{
    namespace
    {
        constexpr const char* DocumentFile = "ui/hud.ui";
        constexpr const char* StylesheetFile = "ui/hud.css";

        /**
         * @brief Where the HUD's files are read from: the source tree's examples/UIDemo/assets while it is there, so
         * an edit to the stylesheet there is what hot reload shows and a rebuild keeps, and the asset root otherwise.
         */
        std::filesystem::path HudFileRoot(const Fenrir::AssetStore& store)
        {
#ifdef UIDEMO_SOURCE_ASSETS
            const std::filesystem::path source{UIDEMO_SOURCE_ASSETS};
            std::error_code error;
            if (std::filesystem::exists(source / StylesheetFile, error))
            {
                return source;
            }
#endif
            return store.Root();
        }

        /** @brief The element of `root` with id `id` as a `T`, or null when there is none or it is another class. */
        template <typename T>
        T* As(const Fenrir::UIRoot& root, Fenrir::ElementId element)
        {
            return dynamic_cast<T*>(root.GetElement(element));
        }
    } // namespace

    Hud::Hud(Fenrir::App& app, const HudConfig& config) : m_app(app)
    {
        auto& roots = app.GetResource<Fenrir::UIRoots>();
        roots.RegisterElement("compass", [] { return std::make_unique<Compass>(); });

        Fenrir::UIRootConfig rootConfig;
        rootConfig.layer = HudLayer;
        rootConfig.target = config.target;
        rootConfig.hotReload = true;
        m_root = &roots.CreateRoot(rootConfig);

        const std::filesystem::path files = HudFileRoot(app.GetResource<Fenrir::AssetStore>());
        const std::filesystem::path stylesheet = files / StylesheetFile;
        m_stylesheetPath = stylesheet.generic_string();
        m_stylesheet = m_root->LoadStylesheet(m_stylesheetPath);
        const Fenrir::MarkupLoad load =
            m_root->LoadDocument(m_root->GetRootElement(), (files / DocumentFile).generic_string());

        // FenrirUI logs each problem as a warning; a HUD missing a part is an error, so a smoke run fails on it.
        if (!m_stylesheet.IsValid())
        {
            app.Logger()->Error("UIDemo: the HUD's stylesheet {} did not load", m_stylesheetPath);
        }
        else if (const Fenrir::Stylesheet* sheet = m_root->GetStylesheet(m_stylesheet);
                 sheet != nullptr && !sheet->diagnostics.empty())
        {
            app.Logger()->Error("UIDemo: the HUD's stylesheet {} has {} problems, logged above", m_stylesheetPath,
                                sheet->diagnostics.size());
        }
        if (!load.element.IsValid() || !load.errors.empty())
        {
            app.Logger()->Error("UIDemo: the HUD's document {} loaded with {} problems, logged above",
                                (files / DocumentFile).generic_string(), load.errors.size());
        }
        app.Logger()->Info("UIDemo: the HUD's stylesheet is {}; edit it while the demo runs to restyle the HUD",
                           m_stylesheetPath);
    }

    Hud::~Hud()
    {
        if (auto* roots = m_app.TryGetResource<Fenrir::UIRoots>(); roots != nullptr && m_root != nullptr)
        {
            roots->DestroyRoot(*m_root);
        }
    }

    Fenrir::ElementId Hud::Find(const char* id) const
    {
        // Looked up each time, not kept: a hot-reloaded document makes new elements under the same ids.
        return m_root->FindById(id);
    }

    void Hud::Show(const HudFrame& frame)
    {
        Fenrir::UIRoot& root = *m_root;
        if (auto* health = As<Fenrir::ProgressBar>(root, Find("health")))
        {
            health->SetRange(0.0f, frame.health.max);
            health->SetValue(frame.health.current);
        }
        if (auto* stamina = As<Fenrir::ProgressBar>(root, Find("stamina")))
        {
            stamina->SetRange(0.0f, frame.stamina.max);
            stamina->SetValue(frame.stamina.current);
        }

        const Fenrir::ElementId ammo = Find("ammo");
        const std::string ammoText = std::format("{} / {}", frame.ammo.loaded, frame.ammo.reserve);
        if (ammo.IsValid() && root.GetText(ammo) != ammoText)
        {
            root.SetText(ammo, ammoText);
        }

        if (auto* compass = As<Compass>(root, Find("compass")))
        {
            compass->SetHeading(frame.heading);
        }

        if (auto* minimap = As<Fenrir::ImageView>(root, Find("minimap"));
            minimap != nullptr && minimap->GetSource() != frame.minimap)
        {
            minimap->SetSource(frame.minimap);
        }
        // The map is north-up; the player's arrow on it turns with the heading.
        // Written only when it changes: an inline style change restyles the arrow. A reloaded document's new arrow
        // has another id, so it is written once more.
        const Fenrir::ElementId arrow = Find("minimap-player");
        std::string transform = std::format("rotate({:.0f}deg)", frame.heading);
        if (arrow.IsValid() && (arrow != m_arrow || transform != m_arrowTransform))
        {
            root.SetStyleProperty(arrow, "transform", transform);
            m_arrow = arrow;
            m_arrowTransform = std::move(transform);
        }
    }

    void Hud::AddHitMarker()
    {
        Fenrir::UIRoot& root = *m_root;
        const Fenrir::ElementId markers = Find("hit-markers");
        if (!markers.IsValid())
        {
            return;
        }
        // Four strokes round the centre, which the stylesheet places and turns; the marker's :intro and :outro
        // transitions animate it in and out.
        const Fenrir::ElementId marker = root.Create(markers, "panel");
        root.AddClass(marker, "hit-marker");
        for (const char* corner : {"top-left", "top-right", "bottom-left", "bottom-right"})
        {
            const Fenrir::ElementId stroke = root.Create(marker, "panel");
            root.AddClass(stroke, "hit-stroke");
            root.AddClass(stroke, corner);
        }
        m_hitMarkers.push_back({marker, 0.0f});
    }

    void Hud::Update(float seconds)
    {
        for (HitMarker& marker : m_hitMarkers)
        {
            marker.age += seconds;
            if (marker.age >= HitMarkerSeconds)
            {
                // Removal waits for the marker's outro (ADR-0066).
                m_root->Remove(marker.element);
            }
        }
        std::erase_if(m_hitMarkers, [](const HitMarker& marker) { return marker.age >= HitMarkerSeconds; });
    }
} // namespace UIDemo
