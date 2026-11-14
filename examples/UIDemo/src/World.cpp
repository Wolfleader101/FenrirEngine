#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <format>
#include <memory>
#include <string>
#include <string_view>

#include "DemoUI.hpp"
#include "FenrirApp/App.hpp"
#include "FenrirAssets/AssetStore.hpp"
#include "FenrirUI/Controls.hpp"
#include "FenrirUI/Markup.hpp"
#include "FenrirUI/UI.hpp"
#include "FenrirUI/UIRoot.hpp"
#include "FenrirUI/UIWorldTarget.hpp"
#include "UIDemo.hpp"

namespace UIDemo
{
    namespace
    {
        constexpr const char* TerminalDocument = "ui/terminal.ui";
        constexpr const char* PlateDocument = "ui/plate.ui";
        constexpr const char* StylesheetFile = "ui/world.css";
        /** @brief How strongly the hue slider's colours are saturated, 0 to 1. */
        constexpr float HueSaturation = 0.6f;

        /** @brief The sRGB channels of hue `degrees` at HueSaturation and full value. */
        std::array<float, 3> HueRgb(float degrees)
        {
            const float h = std::fmod(std::max(degrees, 0.0f), 360.0f) / 60.0f;
            const float chroma = HueSaturation;
            const float x = chroma * (1.0f - std::abs(std::fmod(h, 2.0f) - 1.0f));
            const float low = 1.0f - chroma;
            std::array<float, 3> rgb{};
            switch (static_cast<int>(h))
            {
            case 0:
                rgb = {chroma, x, 0.0f};
                break;
            case 1:
                rgb = {x, chroma, 0.0f};
                break;
            case 2:
                rgb = {0.0f, chroma, x};
                break;
            case 3:
                rgb = {0.0f, x, chroma};
                break;
            case 4:
                rgb = {x, 0.0f, chroma};
                break;
            default:
                rgb = {chroma, 0.0f, x};
                break;
            }
            return {rgb[0] + low, rgb[1] + low, rgb[2] + low};
        }

        /** @brief `rgb`'s sRGB channels packed as Colour::FromSrgb takes them, opaque. */
        std::uint32_t Pack(const std::array<float, 3>& rgb)
        {
            std::uint32_t packed = 0;
            for (const float channel : rgb)
            {
                packed =
                    (packed << 8u) | static_cast<std::uint32_t>(std::lround(std::clamp(channel, 0.0f, 1.0f) * 255));
            }
            return (packed << 8u) | 0xFFu;
        }
    } // namespace

    WorldUI::WorldUI(Fenrir::App& app) : m_app(app), m_light(Fenrir::Colour::FromSrgb(WarmLight))
    {
        m_files = UIFileRoot(app.GetResource<Fenrir::AssetStore>(), StylesheetFile).generic_string();
        // Events sent before the terminal are not its.
        (void)m_events.Read(app);
    }

    WorldUI::~WorldUI()
    {
        DestroyRoots();
    }

    void WorldUI::DestroyRoots()
    {
        if (auto* roots = m_app.TryGetResource<Fenrir::UIRoots>(); roots != nullptr)
        {
            for (const Fenrir::UIRoot* root : {m_terminal, m_plate})
            {
                if (root != nullptr)
                {
                    roots->DestroyRoot(*root);
                }
            }
        }
        m_terminal = nullptr;
        m_plate = nullptr;
    }

    Fenrir::UIRoot& WorldUI::MakeRoot(Fenrir::Entity entity, const char* document)
    {
        // A world root: its scale rule (UI units are the quad's) and blend space (linear) are its target's defaults.
        Fenrir::UIRootConfig config;
        config.target = std::make_shared<Fenrir::UIWorldTarget>(m_app, entity);
        config.hotReload = true;
        Fenrir::UIRoot& root = m_app.GetResource<Fenrir::UIRoots>().CreateRoot(std::move(config));
        const std::string stylesheet = (std::filesystem::path(m_files) / StylesheetFile).generic_string();
        const Fenrir::StylesheetId sheet = root.LoadStylesheet(stylesheet);
        // FenrirUI logs each problem as a warning; a world root missing a part is an error, so a smoke run fails on it.
        if (!sheet.IsValid())
        {
            m_app.Logger()->Error("UIDemo: the world roots' stylesheet {} did not load", stylesheet);
        }
        else if (const Fenrir::Stylesheet* parsed = root.GetStylesheet(sheet);
                 parsed != nullptr && !parsed->diagnostics.empty())
        {
            m_app.Logger()->Error("UIDemo: the world roots' stylesheet {} has {} problems, logged above", stylesheet,
                                  parsed->diagnostics.size());
        }
        const std::string path = (std::filesystem::path(m_files) / document).generic_string();
        const Fenrir::MarkupLoad load = root.LoadDocument(root.GetRootElement(), path);
        if (!load.element.IsValid() || !load.errors.empty())
        {
            m_app.Logger()->Error("UIDemo: the world root's document {} loaded with {} problems, logged above", path,
                                  load.errors.size());
        }
        return root;
    }

    void WorldUI::Attach(Fenrir::Entity terminal, Fenrir::Entity plate)
    {
        DestroyRoots();
        m_terminal = &MakeRoot(terminal, TerminalDocument);
        m_plate = &MakeRoot(plate, PlateDocument);
        if (auto* slider = As<Fenrir::Slider>(*m_terminal, m_terminal->FindById("hue")))
        {
            slider->SetValue(m_hue);
        }
        SetLight(m_lightSrgb, std::string(m_reading));
    }

    void WorldUI::SetLight(std::uint32_t srgb, std::string_view reading)
    {
        m_lightSrgb = srgb;
        m_reading = reading;
        m_light = Fenrir::Colour::FromSrgb(srgb);
        if (m_terminal == nullptr)
        {
            return;
        }
        (void)m_terminal->SetStyleProperty(m_terminal->FindById("swatch"), "background-color",
                                           std::format("#{:06x}", srgb >> 8u));
        m_terminal->SetText(m_terminal->FindById("reading"), reading);
    }

    bool WorldUI::Update(const std::optional<Fenrir::Math::Ray>& ray, bool pressed)
    {
        if (m_terminal == nullptr)
        {
            (void)m_events.Read(m_app);
            return false;
        }
        const bool over = m_terminal->SetPointerRay(ray, pressed);
        const Fenrir::UIRootId terminal = m_terminal->GetId();
        for (const Fenrir::UIEvent& event : m_events.Read(m_app))
        {
            if (event.root != terminal)
            {
                continue;
            }
            const std::string_view id = m_terminal->GetIdString(event.target);
            if (event.kind == Fenrir::UIEventKind::Click && id == "warm")
            {
                SetLight(WarmLight, "Warm white");
            }
            else if (event.kind == Fenrir::UIEventKind::Click && id == "cool")
            {
                SetLight(CoolLight, "Cool white");
            }
            else if ((event.kind == Fenrir::UIEventKind::Click && id == "hue-on") ||
                     (event.kind == Fenrir::UIEventKind::Change && id == "hue"))
            {
                if (event.kind == Fenrir::UIEventKind::Change)
                {
                    m_hue = event.value;
                }
                SetLight(Pack(HueRgb(m_hue)), std::format("Hue {:.0f} degrees", m_hue));
            }
        }
        return over;
    }
} // namespace UIDemo
