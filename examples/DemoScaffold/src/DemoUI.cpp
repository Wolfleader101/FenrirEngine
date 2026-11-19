#include "FenrirDemoScaffold/DemoUI.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <numbers>
#include <optional>
#include <utility>

#include "FenrirApp/App.hpp"
#include "FenrirInput/Devices.hpp"
#include "FenrirLogger/ILogger.hpp"
#include "FenrirUI/Controls.hpp"
#include "FenrirUI/Markup.hpp"
#include "FenrirUI/UI.hpp"
#include "FenrirUI/UIEvent.hpp"
#include "FenrirUI/UIRoot.hpp"

namespace FenrirDemo
{
    namespace
    {
        constexpr const char* DocumentFile = "ui/demo.ui";
        constexpr const char* StylesheetFile = "ui/demo.css";
        // What a scene slot shows with no script to mount a Razor panel into it (ADR-0072).
        constexpr const char* SlotNote =
            "This panel is a Razor component and needs scripting (FENRIR_ENABLE_SCRIPTING); "
            "its keys are in the help.";

        // The build stage order of the kit's work: early, so a scene's rows pushed later in the stage win.
        constexpr std::int32_t BuildStageOrder = -100;

        /** @brief Logs an error for a sheet of the kit's that did not load or loaded with problems. */
        void ReportStylesheet(Fenrir::App& app, const Fenrir::UIRoot& root, Fenrir::StylesheetId sheet,
                              const std::string& path)
        {
            if (!sheet.IsValid())
            {
                app.Logger()->Error("The demo UI kit's stylesheet {} did not load", path);
            }
            else if (const Fenrir::Stylesheet* loaded = root.GetStylesheet(sheet);
                     loaded != nullptr && !loaded->diagnostics.empty())
            {
                app.Logger()->Error("The demo UI kit's stylesheet {} has {} problems, logged above", path,
                                    loaded->diagnostics.size());
            }
        }

        /** @brief `element` of `root` as a `Control`, or null when it is gone or is another kind. */
        template <typename Control>
        Control* As(const Fenrir::UIRoot& root, Fenrir::ElementId element)
        {
            return dynamic_cast<Control*>(root.GetElement(element));
        }

        /** @brief Makes a `tag` under `parent` with the class `name`. */
        Fenrir::ElementId Make(Fenrir::UIRoot& root, Fenrir::ElementId parent, std::string_view tag,
                               std::string_view name)
        {
            const Fenrir::ElementId element = root.Create(parent, tag);
            root.AddClass(element, name);
            return element;
        }

        /** @brief A label of class `name` under `parent`, showing `text`. */
        Fenrir::ElementId MakeLabel(Fenrir::UIRoot& root, Fenrir::ElementId parent, std::string_view name,
                                    std::string_view text)
        {
            const Fenrir::ElementId label = Make(root, parent, "label", name);
            root.SetText(label, text);
            return label;
        }

        /** @brief A key row under `parent`: a panel of class `row` holding `keys` and `what` labels of their classes.
         */
        void MakeKeyRow(Fenrir::UIRoot& root, Fenrir::ElementId parent, const DemoKeyRow& keyRow, std::string_view row,
                        std::string_view keys, std::string_view what)
        {
            const Fenrir::ElementId line = Make(root, parent, "panel", row);
            MakeLabel(root, line, keys, keyRow.keys);
            MakeLabel(root, line, what, keyRow.what);
        }

        /** @brief Removes every child of `parent` at once, for a part of the kit made whole again. */
        void RemoveChildren(Fenrir::UIRoot& root, Fenrir::ElementId parent)
        {
            for (const Fenrir::ElementId child :
                 std::vector<Fenrir::ElementId>(root.GetChildren(parent).begin(), root.GetChildren(parent).end()))
            {
                (void)root.Remove(child, Fenrir::RemoveMode::Immediate);
            }
        }

        /** @brief A slider of the class `demo-slider`, and `name` when given, from `min` to `max` in `step`s. */
        Fenrir::ElementId MakeSlider(Fenrir::UIRoot& root, Fenrir::ElementId parent, float min, float max, float step,
                                     std::string_view name = {})
        {
            const Fenrir::ElementId element = Make(root, parent, "slider", "demo-slider");
            if (!name.empty())
            {
                root.AddClass(element, name);
            }
            if (auto* slider = As<Fenrir::Slider>(root, element); slider != nullptr)
            {
                slider->SetRange(min, max);
                slider->SetStep(step);
            }
            return element;
        }

        /** @brief Whether a press holds `element` (a slider being dragged). */
        bool Held(const Fenrir::UIRoot& root, Fenrir::ElementId element)
        {
            return root.HasPseudoClass(element, Fenrir::PseudoClass::Active);
        }

        /** @brief A linear colour component encoded for display (sRGB's transfer function), 0 to 255. */
        int EncodeSrgb(float linear)
        {
            const float clamped = std::clamp(linear, 0.0f, 1.0f);
            const float encoded =
                clamped <= 0.0031308f ? clamped * 12.92f : 1.055f * std::pow(clamped, 1.0f / 2.4f) - 0.055f;
            return static_cast<int>(std::lround(encoded * 255.0f));
        }

        /** @brief Hue, saturation and value, each 0 to 1, of `rgb`. A grey keeps hue 0. */
        Fenrir::Math::Vec3 RgbToHsv(Fenrir::Math::Vec3 rgb)
        {
            const float high = std::max({rgb.r, rgb.g, rgb.b});
            const float low = std::min({rgb.r, rgb.g, rgb.b});
            const float range = high - low;
            float hue = 0.0f;
            if (range > 0.0f)
            {
                if (high == rgb.r)
                {
                    hue = std::fmod((rgb.g - rgb.b) / range, 6.0f);
                }
                else if (high == rgb.g)
                {
                    hue = (rgb.b - rgb.r) / range + 2.0f;
                }
                else
                {
                    hue = (rgb.r - rgb.g) / range + 4.0f;
                }
                hue /= 6.0f;
                if (hue < 0.0f)
                {
                    hue += 1.0f;
                }
            }
            return {hue, high > 0.0f ? range / high : 0.0f, high};
        }

        /** @brief The colour of hue, saturation and value `hsv`, each 0 to 1. */
        Fenrir::Math::Vec3 HsvToRgb(Fenrir::Math::Vec3 hsv)
        {
            const float sector = std::fmod(hsv.x, 1.0f) * 6.0f;
            const float chroma = hsv.z * hsv.y;
            const float second = chroma * (1.0f - std::abs(std::fmod(sector, 2.0f) - 1.0f));
            Fenrir::Math::Vec3 rgb(0.0f);
            switch (static_cast<int>(sector))
            {
            case 0:
                rgb = {chroma, second, 0.0f};
                break;
            case 1:
                rgb = {second, chroma, 0.0f};
                break;
            case 2:
                rgb = {0.0f, chroma, second};
                break;
            case 3:
                rgb = {0.0f, second, chroma};
                break;
            case 4:
                rgb = {second, 0.0f, chroma};
                break;
            default:
                rgb = {chroma, 0.0f, second};
                break;
            }
            return rgb + Fenrir::Math::Vec3(hsv.z - chroma);
        }
    } // namespace

    namespace Detail
    {
        /**
         * @brief A switch row's control and its binding. Build makes the row's elements under its row element and
         * attaches the listeners that write a change back the frame it arrives; Sync, in the build stage, shows the
         * bound value in the control unless it is held or edited, and only when it differs from what was last shown,
         * so a hue survives a grey and a drag is never fought.
         *
         * The listeners hold `this`: a switch is shared by its row and outlives the row's elements, which go with
         * their listeners when the row's section does or the document loads again.
         */
        class DemoSwitch
        {
          public:
            DemoSwitch() = default;
            DemoSwitch(const DemoSwitch&) = delete;
            DemoSwitch& operator=(const DemoSwitch&) = delete;
            DemoSwitch(DemoSwitch&&) = delete;
            DemoSwitch& operator=(DemoSwitch&&) = delete;
            virtual ~DemoSwitch() = default;

            /** @brief Makes the row's label and controls under `row`, its element, of a row labelled `label`. */
            virtual void Build(Fenrir::UIRoot& root, Fenrir::ElementId row, const std::string& label) = 0;
            /** @brief Shows the bound value in the controls, unless one is held or edited or it is already shown. */
            virtual void Sync(Fenrir::UIRoot& root) = 0;

            /** @brief Disables or enables every control of the row. */
            void SetDisabled(const Fenrir::UIRoot& root, bool disabled) const
            {
                for (const Fenrir::ElementId element : m_controls)
                {
                    if (auto* control = As<Fenrir::Control>(root, element); control != nullptr)
                    {
                        control->SetDisabled(disabled);
                    }
                }
            }

          protected:
            /** @brief The row's controls, for SetDisabled; Build fills it. */
            std::vector<Fenrir::ElementId> m_controls;
        };
    } // namespace Detail

    namespace
    {
        /** @brief Gives a switch row's element `row` its kind's class and makes its label. */
        void StartSwitchRow(Fenrir::UIRoot& root, Fenrir::ElementId row, std::string_view kind,
                            const std::string& label)
        {
            root.AddClass(row, kind);
            MakeLabel(root, row, "demo-row-label", label);
        }

        /** @brief A checkbox over a bool. */
        class ToggleSwitch final : public Detail::DemoSwitch
        {
          public:
            explicit ToggleSwitch(DemoValue<bool> value) : m_value(std::move(value))
            {
            }

            void Build(Fenrir::UIRoot& root, Fenrir::ElementId row, const std::string& label) override
            {
                StartSwitchRow(root, row, "demo-toggle", label);
                m_box = Make(root, row, "checkbox", "demo-checkbox");
                m_controls = {m_box};
                m_shown.reset();
                root.AddListener(m_box, Fenrir::UIEventKind::Change, [this](Fenrir::UIListenerContext& context) {
                    const bool checked = context.GetEvent().value != 0.0f;
                    m_shown = checked;
                    m_value.Set(checked);
                });
            }

            void Sync(Fenrir::UIRoot& root) override
            {
                const bool value = m_value.Get();
                if (m_shown == value)
                {
                    return;
                }
                m_shown = value;
                if (auto* box = As<Fenrir::Checkbox>(root, m_box); box != nullptr)
                {
                    box->SetChecked(value);
                }
            }

          private:
            DemoValue<bool> m_value;
            Fenrir::ElementId m_box;
            std::optional<bool> m_shown;
        };

        /** @brief A slider over a float, linear or logarithmic, its value shown beside it. */
        class RangeSwitch final : public Detail::DemoSwitch
        {
          public:
            RangeSwitch(DemoValue<float> value, float min, float max, DemoRangeOptions options)
                : m_value(std::move(value)), m_min(min), m_max(max), m_options(options)
            {
            }

            void Build(Fenrir::UIRoot& root, Fenrir::ElementId row, const std::string& label) override
            {
                StartSwitchRow(root, row, "demo-range", label);
                // A logarithmic slider runs over t, 0 to 1; a linear one over the value itself.
                m_slider = m_options.logarithmic ? MakeSlider(root, row, 0.0f, 1.0f, 0.0f)
                                                 : MakeSlider(root, row, m_min, m_max, m_options.step);
                m_text = MakeLabel(root, row, "demo-range-value", "");
                m_controls = {m_slider};
                m_shown.reset();
                root.AddListener(m_slider, Fenrir::UIEventKind::Change,
                                 [this, &root](Fenrir::UIListenerContext& context) {
                                     const float value = FromSlider(context.GetEvent().value);
                                     m_shown = value;
                                     m_value.Set(value);
                                     root.SetText(m_text, Format(value));
                                 });
            }

            void Sync(Fenrir::UIRoot& root) override
            {
                if (Held(root, m_slider))
                {
                    return;
                }
                const float value = m_value.Get();
                if (m_shown == value)
                {
                    return;
                }
                m_shown = value;
                if (auto* slider = As<Fenrir::Slider>(root, m_slider); slider != nullptr)
                {
                    slider->SetValue(ToSlider(value));
                }
                root.SetText(m_text, Format(value));
            }

          private:
            [[nodiscard]] float FromSlider(float position) const
            {
                return m_options.logarithmic ? m_min * std::pow(m_max / m_min, position) : position;
            }

            [[nodiscard]] float ToSlider(float value) const
            {
                if (!m_options.logarithmic)
                {
                    return value;
                }
                return value > 0.0f ? std::log(value / m_min) / std::log(m_max / m_min) : 0.0f;
            }

            [[nodiscard]] std::string Format(float value) const
            {
                const int decimals = std::max(m_options.decimals, 0);
                if (m_options.angle)
                {
                    // U+00B0, the degree sign, in UTF-8.
                    return std::format("{:.{}f}\xC2\xB0", value * 180.0f / std::numbers::pi_v<float>, decimals);
                }
                return std::format("{:.{}f}", value, decimals);
            }

            DemoValue<float> m_value;
            float m_min;
            float m_max;
            DemoRangeOptions m_options;
            Fenrir::ElementId m_slider;
            Fenrir::ElementId m_text;
            std::optional<float> m_shown;
        };

        /** @brief One of a list of options: buttons for up to four, a stepper round the chosen one's name beyond. */
        class ChoiceSwitch final : public Detail::DemoSwitch
        {
          public:
            /** @brief The most options shown as buttons. */
            static constexpr std::size_t MaxButtons = 4;

            ChoiceSwitch(DemoValue<int> value, std::vector<std::string> options)
                : m_value(std::move(value)), m_options(std::move(options))
            {
            }

            void Build(Fenrir::UIRoot& root, Fenrir::ElementId row, const std::string& label) override
            {
                StartSwitchRow(root, row, "demo-choice", label);
                m_buttons.clear();
                m_shown.reset();
                if (m_options.size() <= MaxButtons)
                {
                    const Fenrir::ElementId buttons = Make(root, row, "panel", "demo-choice-buttons");
                    for (std::size_t i = 0; i < m_options.size(); ++i)
                    {
                        const Fenrir::ElementId button = Make(root, buttons, "button", "demo-choice-button");
                        root.SetText(button, m_options[i]);
                        root.AddListener(button, Fenrir::UIEventKind::Click,
                                         [this, i](Fenrir::UIListenerContext&) { Choose(static_cast<int>(i)); });
                        m_buttons.push_back(button);
                    }
                    m_controls = m_buttons;
                    return;
                }
                const Fenrir::ElementId stepper = Make(root, row, "panel", "demo-stepper");
                const Fenrir::ElementId previous = Make(root, stepper, "button", "demo-step-previous");
                root.SetText(previous, "<");
                m_name = MakeLabel(root, stepper, "demo-choice-name", "");
                const Fenrir::ElementId next = Make(root, stepper, "button", "demo-step-next");
                root.SetText(next, ">");
                root.AddListener(previous, Fenrir::UIEventKind::Click,
                                 [this](Fenrir::UIListenerContext&) { Step(-1); });
                root.AddListener(next, Fenrir::UIEventKind::Click, [this](Fenrir::UIListenerContext&) { Step(1); });
                m_controls = {previous, next};
            }

            void Sync(Fenrir::UIRoot& root) override
            {
                const int value = m_value.Get();
                if (m_shown == value)
                {
                    return;
                }
                m_shown = value;
                for (std::size_t i = 0; i < m_buttons.size(); ++i)
                {
                    if (std::cmp_equal(i, value))
                    {
                        root.AddClass(m_buttons[i], "active");
                    }
                    else
                    {
                        root.RemoveClass(m_buttons[i], "active");
                    }
                }
                if (m_buttons.empty())
                {
                    root.SetText(m_name, value >= 0 && std::cmp_less(value, m_options.size()) ? m_options[value] : "");
                }
            }

          private:
            /** @brief Writes `index`; the next Sync marks it. */
            void Choose(int index) const
            {
                m_value.Set(index);
            }

            /** @brief Moves `by` options from the chosen one, wrapping round the ends. */
            void Step(int by) const
            {
                const int count = static_cast<int>(m_options.size());
                const int chosen = std::clamp(m_value.Get(), 0, count - 1);
                Choose(((chosen + by) % count + count) % count);
            }

            DemoValue<int> m_value;
            std::vector<std::string> m_options;
            std::vector<Fenrir::ElementId> m_buttons;
            Fenrir::ElementId m_name;
            std::optional<int> m_shown;
        };

        /** @brief A linear colour: a swatch of it, encoded for display, over hue, saturation and value sliders. */
        class ColourSwitch final : public Detail::DemoSwitch
        {
          public:
            explicit ColourSwitch(DemoValue<Fenrir::Math::Vec3> value) : m_value(std::move(value))
            {
            }

            void Build(Fenrir::UIRoot& root, Fenrir::ElementId row, const std::string& label) override
            {
                StartSwitchRow(root, row, "demo-colour", label);
                m_swatch = Make(root, row, "panel", "demo-swatch");
                const Fenrir::ElementId sliders = Make(root, row, "panel", "demo-colour-sliders");
                constexpr std::array<std::pair<const char*, const char*>, 3> channels{
                    {{"H", "demo-hue"}, {"S", "demo-saturation"}, {"V", "demo-value"}}};
                for (std::size_t i = 0; i < channels.size(); ++i)
                {
                    const Fenrir::ElementId line = Make(root, sliders, "panel", "demo-colour-channel");
                    MakeLabel(root, line, "demo-colour-channel-label", channels[i].first);
                    m_sliders[i] = MakeSlider(root, line, 0.0f, 1.0f, 0.0f, channels[i].second);
                    root.AddListener(m_sliders[i], Fenrir::UIEventKind::Change,
                                     [this, &root](Fenrir::UIListenerContext&) { FromSliders(root); });
                }
                m_controls.assign(m_sliders.begin(), m_sliders.end());
                m_shown.reset();
            }

            void Sync(Fenrir::UIRoot& root) override
            {
                if (std::any_of(m_sliders.begin(), m_sliders.end(),
                                [&root](Fenrir::ElementId slider) { return Held(root, slider); }))
                {
                    return;
                }
                const Fenrir::Math::Vec3 colour = m_value.Get();
                if (m_shown == colour)
                {
                    return;
                }
                m_shown = colour;
                const Fenrir::Math::Vec3 hsv = RgbToHsv(colour);
                for (std::size_t i = 0; i < m_sliders.size(); ++i)
                {
                    if (auto* slider = As<Fenrir::Slider>(root, m_sliders[i]); slider != nullptr)
                    {
                        slider->SetValue(hsv[static_cast<int>(i)]);
                    }
                }
                ShowSwatch(root, colour);
            }

          private:
            /** @brief The sliders changed: the colour they make is written and shown. */
            void FromSliders(Fenrir::UIRoot& root)
            {
                Fenrir::Math::Vec3 hsv(0.0f);
                for (std::size_t i = 0; i < m_sliders.size(); ++i)
                {
                    if (const auto* slider = As<Fenrir::Slider>(root, m_sliders[i]); slider != nullptr)
                    {
                        hsv[static_cast<int>(i)] = slider->GetValue();
                    }
                }
                const Fenrir::Math::Vec3 colour = HsvToRgb(hsv);
                m_shown = colour;
                m_value.Set(colour);
                ShowSwatch(root, colour);
            }

            void ShowSwatch(Fenrir::UIRoot& root, Fenrir::Math::Vec3 colour) const
            {
                // CSS colours are sRGB-encoded (ADR-0065): the linear colour is encoded for the swatch.
                (void)root.SetStyleProperty(m_swatch, "background-color",
                                            std::format("#{:02x}{:02x}{:02x}", EncodeSrgb(colour.r),
                                                        EncodeSrgb(colour.g), EncodeSrgb(colour.b)));
            }

            DemoValue<Fenrir::Math::Vec3> m_value;
            Fenrir::ElementId m_swatch;
            std::array<Fenrir::ElementId, 3> m_sliders{};
            std::optional<Fenrir::Math::Vec3> m_shown;
        };

        /** @brief A button that calls a function. */
        class ButtonSwitch final : public Detail::DemoSwitch
        {
          public:
            explicit ButtonSwitch(std::function<void()> pressed) : m_pressed(std::move(pressed))
            {
            }

            void Build(Fenrir::UIRoot& root, Fenrir::ElementId row, const std::string& label) override
            {
                root.AddClass(row, "demo-button-row");
                const Fenrir::ElementId button = Make(root, row, "button", "demo-button");
                root.SetText(button, label);
                m_controls = {button};
                root.AddListener(button, Fenrir::UIEventKind::Click, [this](Fenrir::UIListenerContext&) {
                    // Copied: the function may leave the scene, which removes this row and this switch with it.
                    const std::function<void()> pressed = m_pressed;
                    if (pressed)
                    {
                        pressed();
                    }
                });
            }

            void Sync(Fenrir::UIRoot&) override
            {
            }

          private:
            std::function<void()> m_pressed;
        };

        /** @brief A text entry over a string, written at each edit and not changed by code while it is edited. */
        class TextEntrySwitch final : public Detail::DemoSwitch
        {
          public:
            explicit TextEntrySwitch(DemoValue<std::string> value) : m_value(std::move(value))
            {
            }

            void Build(Fenrir::UIRoot& root, Fenrir::ElementId row, const std::string& label) override
            {
                StartSwitchRow(root, row, "demo-text-entry", label);
                m_entry = Make(root, row, "textentry", "demo-entry");
                m_controls = {m_entry};
                m_shown.reset();
                root.AddListener(m_entry, Fenrir::UIEventKind::Change, [this, &root](Fenrir::UIListenerContext&) {
                    if (const auto* entry = As<Fenrir::TextEntry>(root, m_entry); entry != nullptr)
                    {
                        m_shown = std::string(entry->GetValue());
                        m_value.Set(*m_shown);
                    }
                });
            }

            void Sync(Fenrir::UIRoot& root) override
            {
                if (root.GetFocused() == m_entry)
                {
                    return;
                }
                std::string value = m_value.Get();
                if (m_shown == value)
                {
                    return;
                }
                if (auto* entry = As<Fenrir::TextEntry>(root, m_entry); entry != nullptr)
                {
                    entry->SetValue(value);
                }
                m_shown = std::move(value);
            }

          private:
            DemoValue<std::string> m_value;
            Fenrir::ElementId m_entry;
            std::optional<std::string> m_shown;
        };
    } // namespace

    // Rows and sections.

    void DemoRow::SetShown(bool shown) const
    {
        if (DemoUI::Row* row = m_ui != nullptr ? m_ui->FindRow(m_id) : nullptr; row != nullptr && row->shown != shown)
        {
            row->shown = shown;
            m_ui->ApplyRow(*row);
        }
    }

    void DemoRow::SetDisabled(bool disabled) const
    {
        if (DemoUI::Row* row = m_ui != nullptr ? m_ui->FindRow(m_id) : nullptr;
            row != nullptr && row->disabled != disabled)
        {
            row->disabled = disabled;
            m_ui->ApplyRow(*row);
        }
    }

    bool DemoRow::IsValid() const
    {
        return m_ui != nullptr && m_ui->FindRow(m_id) != nullptr;
    }

    void DemoTextRow::Set(std::string_view line) const
    {
        if (DemoUI::Row* row = m_ui != nullptr ? m_ui->FindRow(m_id) : nullptr; row != nullptr)
        {
            m_ui->SetRowText(*row, line);
        }
    }

    void DemoReadout::Set(std::string_view value) const
    {
        if (DemoUI::Row* row = m_ui != nullptr ? m_ui->FindRow(m_id) : nullptr; row != nullptr)
        {
            m_ui->SetRowText(*row, value);
        }
    }

    DemoTextRow DemoSection::Text(std::string line) const
    {
        const DemoUI::Row* row =
            m_ui != nullptr ? m_ui->AddRow(m_id, {.kind = DemoUI::RowKind::Text, .text = std::move(line)}) : nullptr;
        return row != nullptr ? DemoTextRow(m_ui, row->id) : DemoTextRow();
    }

    DemoReadout DemoSection::Readout(std::string label) const
    {
        return Readout(std::move(label), {});
    }

    DemoReadout DemoSection::Readout(std::string label, std::function<std::string()> value) const
    {
        const DemoUI::Row* row =
            m_ui != nullptr
                ? m_ui->AddRow(m_id,
                               {.kind = DemoUI::RowKind::Readout, .label = std::move(label), .pull = std::move(value)})
                : nullptr;
        return row != nullptr ? DemoReadout(m_ui, row->id) : DemoReadout();
    }

    DemoRow DemoSection::Separator(std::string title) const
    {
        const DemoUI::Row* row =
            m_ui != nullptr ? m_ui->AddRow(m_id, {.kind = DemoUI::RowKind::Separator, .text = std::move(title)})
                            : nullptr;
        return row != nullptr ? DemoRow(m_ui, row->id) : DemoRow();
    }

    DemoRow DemoSection::AddSwitch(std::string label, std::shared_ptr<Detail::DemoSwitch> control,
                                   std::string hint) const
    {
        return m_ui != nullptr ? m_ui->AddSwitch(m_id, std::move(label), std::move(control), std::move(hint))
                               : DemoRow();
    }

    DemoRow DemoSection::Toggle(std::string label, DemoValue<bool> value, std::string hint) const
    {
        return AddSwitch(std::move(label), std::make_shared<ToggleSwitch>(std::move(value)), std::move(hint));
    }

    DemoRow DemoSection::Range(std::string label, DemoValue<float> value, float min, float max,
                               DemoRangeOptions options, std::string hint) const
    {
        if (m_ui == nullptr)
        {
            return {};
        }
        if (options.logarithmic && (min <= 0.0f || max <= 0.0f))
        {
            m_ui->m_app.Logger()->Error("The demo range \"{}\" is logarithmic over {} to {}, which needs a positive "
                                        "min and max: it is drawn linear",
                                        label, min, max);
            options.logarithmic = false;
        }
        return AddSwitch(std::move(label), std::make_shared<RangeSwitch>(std::move(value), min, max, options),
                         std::move(hint));
    }

    DemoRow DemoSection::Range(std::string label, DemoValue<int> value, int min, int max, std::string hint) const
    {
        // A float range in steps of one, over the int.
        auto shared = std::make_shared<DemoValue<int>>(std::move(value));
        DemoValue<float> asFloat([shared] { return static_cast<float>(shared->Get()); },
                                 [shared](const float& next) { shared->Set(static_cast<int>(std::lround(next))); });
        return Range(std::move(label), std::move(asFloat), static_cast<float>(min), static_cast<float>(max),
                     {.step = 1.0f, .decimals = 0}, std::move(hint));
    }

    DemoRow DemoSection::Choice(std::string label, DemoValue<int> value, std::vector<std::string> options,
                                std::string hint) const
    {
        return AddSwitch(std::move(label), std::make_shared<ChoiceSwitch>(std::move(value), std::move(options)),
                         std::move(hint));
    }

    DemoRow DemoSection::Colour(std::string label, DemoValue<Fenrir::Math::Vec3> linear, std::string hint) const
    {
        return AddSwitch(std::move(label), std::make_shared<ColourSwitch>(std::move(linear)), std::move(hint));
    }

    DemoRow DemoSection::Button(std::string label, std::function<void()> pressed, std::string hint) const
    {
        return AddSwitch(std::move(label), std::make_shared<ButtonSwitch>(std::move(pressed)), std::move(hint));
    }

    DemoRow DemoSection::TextEntry(std::string label, DemoValue<std::string> value, std::string hint) const
    {
        return AddSwitch(std::move(label), std::make_shared<TextEntrySwitch>(std::move(value)), std::move(hint));
    }

    void DemoSection::SetOpen(bool open) const
    {
        if (DemoUI::Section* section = m_ui != nullptr ? m_ui->FindSection(m_id) : nullptr;
            section != nullptr && section->open != open)
        {
            section->open = open;
            m_ui->ApplyOpen(*section);
        }
    }

    bool DemoSection::IsOpen() const
    {
        const DemoUI::Section* section = m_ui != nullptr ? m_ui->FindSection(m_id) : nullptr;
        return section != nullptr && section->open;
    }

    bool DemoSection::IsValid() const
    {
        return m_ui != nullptr && m_ui->FindSection(m_id) != nullptr;
    }

    Fenrir::ElementId DemoSection::Element() const
    {
        const DemoUI::Section* section = m_ui != nullptr ? m_ui->FindSection(m_id) : nullptr;
        return section != nullptr ? section->element : Fenrir::ElementId{};
    }

    // The kit.

    DemoUI::DemoUI(Fenrir::App& app, DemoUIConfig config) : m_app(app), m_config(std::move(config))
    {
        auto& roots = app.GetResource<Fenrir::UIRoots>();
        Fenrir::UIRootConfig rootConfig;
        rootConfig.layer = DemoUILayer;
        rootConfig.inputPriority = DemoUIRouterPriority;
        rootConfig.target = m_config.target;
        rootConfig.hotReload = m_config.hotReload;
        // A pad always drives the scene: the kit is for the mouse and the keyboard.
        rootConfig.gamepadNavigation = false;
        m_root = &roots.CreateRoot(rootConfig);

        const std::filesystem::path& files = m_config.folder;
        m_stylesheetPath = (files / StylesheetFile).generic_string();
        ReportStylesheet(app, *m_root, m_root->LoadStylesheet(m_stylesheetPath), m_stylesheetPath);

        // F1 while a control of the kit has the focus: the root has the keyboard then, so the game's keyboard never
        // sees it.
        m_root->AddListener(m_root->GetRootElement(), Fenrir::UIEventKind::KeyPress,
                            [this](Fenrir::UIListenerContext& context) {
                                // A held key repeats its press; F1 toggles once a press.
                                if (context.GetEvent().value == 0.0f)
                                {
                                    OnKey(context.GetEvent().key);
                                }
                            });
        // A reloaded demo.ui makes the kit's elements again under new ids.
        m_root->AddListener(m_root->GetRootElement(), Fenrir::UIEventKind::DocumentReloaded,
                            [this](Fenrir::UIListenerContext&) { Build(); });
        // The hint line follows the hover and the focus: any change of either marks it, and the build stage sets it.
        for (const Fenrir::UIEventKind kind : {Fenrir::UIEventKind::PointerEnter, Fenrir::UIEventKind::PointerLeave,
                                               Fenrir::UIEventKind::Focus, Fenrir::UIEventKind::Blur})
        {
            m_root->AddListener(m_root->GetRootElement(), kind,
                                [this](Fenrir::UIListenerContext&) { m_hintDirty = true; });
        }
        m_buildStage = roots.AddBuildStage(BuildStageOrder, [this] {
            PollToggleKey();
            ApplyTutorialInput();
            AskTutorialDone();
            PullReadouts();
            SyncSwitches();
            UpdateHint();
        });

        const std::string document = (files / DocumentFile).generic_string();
        const Fenrir::MarkupLoad load = m_root->LoadDocument(m_root->GetRootElement(), document);
        if (!load.element.IsValid() || !load.errors.empty())
        {
            app.Logger()->Error("The demo UI kit's document {} loaded with {} problems, logged above", document,
                                load.errors.size());
        }
        Build();
    }

    DemoUI::~DemoUI()
    {
        if (auto* roots = m_app.TryGetResource<Fenrir::UIRoots>(); roots != nullptr)
        {
            (void)roots->RemoveBuildStage(m_buildStage);
            if (m_root != nullptr)
            {
                (void)roots->DestroyRoot(*m_root);
            }
        }
    }

    Fenrir::UIRoot& DemoUI::Root() const noexcept
    {
        return *m_root;
    }

    void DemoUI::SetVisible(bool visible)
    {
        if (m_visible == visible)
        {
            return;
        }
        m_visible = visible;
        ApplyVisible();
        if (!visible)
        {
            // A hidden kit keeps no keys from the scene.
            m_root->Blur();
        }
        else
        {
            LogHelp();
        }
    }

    Fenrir::StylesheetId DemoUI::AddStylesheet(const std::string& path)
    {
        const Fenrir::StylesheetId sheet = m_root->LoadStylesheet(path);
        ReportStylesheet(m_app, *m_root, sheet, path);
        return sheet;
    }

    void DemoUI::SetTitle(std::string title)
    {
        m_title = std::move(title);
        m_root->SetText(m_panelTitle, m_title);
    }

    DemoSection DemoUI::AppSection(std::string title)
    {
        return AddSection(std::move(title), false);
    }

    DemoSection DemoUI::SceneSection(std::string title)
    {
        return AddSection(std::move(title), true);
    }

    void DemoUI::SceneSlot(std::string id)
    {
        if (id.empty() || std::any_of(m_sections.begin(), m_sections.end(),
                                      [&id](const Section& section) { return section.slot == id; }))
        {
            m_app.Logger()->Error("The demo UI kit's scene slot \"{}\" is empty or there already", id);
            return;
        }
        Section& section = m_sections.emplace_back();
        section.id = m_nextId++;
        section.scene = true;
        section.slot = std::move(id);
        BuildSection(section);
    }

    void DemoUI::ClearScene()
    {
        // The tutorial is the scene's: its steps' `done` may read the scene's state, which goes next. Its card goes at
        // once, so it never fades out beside the next scene's.
        EndTutorial(true);
        for (const Section& section : m_sections)
        {
            if (section.scene)
            {
                (void)m_root->Remove(section.element, Fenrir::RemoveMode::Immediate);
            }
        }
        std::erase_if(m_sections, [](const Section& section) { return section.scene; });
    }

    void DemoUI::SetScenes(std::vector<std::string> names, std::function<void(std::size_t)> enter)
    {
        m_sceneNames = std::move(names);
        m_enterScene = std::move(enter);
        const auto found =
            std::find_if(m_sections.begin(), m_sections.end(), [](const Section& section) { return section.switcher; });
        if (found != m_sections.end())
        {
            (void)m_root->Remove(found->element, Fenrir::RemoveMode::Immediate);
            m_sections.erase(found);
            m_sceneButtons.clear();
        }
        if (m_sceneNames.empty())
        {
            return;
        }
        // The panel's first app section, whenever it is set.
        Section& section = *m_sections.insert(m_sections.begin(), Section{});
        section.id = m_nextId++;
        section.title = "Scenes";
        section.switcher = true;
        BuildSection(section, 0);
    }

    void DemoUI::SetActiveScene(std::size_t index)
    {
        m_activeScene = index;
        ApplyActiveScene();
    }

    void DemoUI::SetHelp(std::string heading, std::vector<DemoKeyRow> rows)
    {
        m_helpHeading = std::move(heading);
        m_helpRows = std::move(rows);
        BuildHelp();
        if (m_visible)
        {
            LogHelp();
        }
    }

    void DemoUI::Build()
    {
        m_kit = m_root->FindById("demo-kit");
        m_panelTitle = m_root->FindById("demo-panel-title");
        m_appSections = m_root->FindById("demo-app-sections");
        m_sceneSections = m_root->FindById("demo-scene-sections");
        m_help = m_root->FindById("demo-help");
        m_hint = m_root->FindById("demo-hint");
        m_tutorialArea = m_root->FindById("demo-tutorial-area");
        // The card went with the document it was in.
        m_tutorialCard = {};
        m_hintDirty = true;
        if (!m_kit.IsValid() || !m_appSections.IsValid() || !m_sceneSections.IsValid() || !m_help.IsValid() ||
            !m_hint.IsValid() || !m_tutorialArea.IsValid())
        {
            m_app.Logger()->Error("The demo UI kit's document lacks #demo-kit, #demo-app-sections, "
                                  "#demo-scene-sections, #demo-help, #demo-hint or #demo-tutorial-area");
        }
        if (m_hint.IsValid())
        {
            Display(m_hint, false);
        }
        m_root->SetText(m_panelTitle, m_title);
        for (Section& section : m_sections)
        {
            BuildSection(section);
        }
        BuildHelp();
        BuildTutorial();
        ApplyVisible();
    }

    DemoSection DemoUI::AddSection(std::string title, bool scene)
    {
        Section& section = m_sections.emplace_back();
        section.id = m_nextId++;
        section.title = std::move(title);
        section.scene = scene;
        BuildSection(section);
        return DemoSection(this, section.id);
    }

    void DemoUI::BuildSection(Section& section, std::size_t index)
    {
        Fenrir::UIRoot& root = *m_root;
        section.element = root.Create(section.scene ? m_sceneSections : m_appSections, "panel", index);
        if (!section.slot.empty())
        {
            root.AddClass(section.element, "demo-slot");
            root.SetIdString(section.element, section.slot);
            if (!m_config.scripting)
            {
                MakeLabel(root, section.element, "demo-slot-note", SlotNote);
            }
            return;
        }
        root.AddClass(section.element, "demo-section");
        const Fenrir::ElementId title = root.Create(section.element, "button");
        root.AddClass(title, "demo-section-title");
        root.SetText(title, section.title);
        const DemoSection handle(this, section.id);
        root.AddListener(title, Fenrir::UIEventKind::Click,
                         [handle](Fenrir::UIListenerContext&) { handle.SetOpen(!handle.IsOpen()); });
        section.body = root.Create(section.element, "panel");
        root.AddClass(section.body, "demo-section-body");
        if (section.switcher)
        {
            BuildSceneButtons(section);
        }
        for (Row& row : section.rows)
        {
            BuildRow(row, section.body);
        }
        ApplyOpen(section);
    }

    DemoUI::Row* DemoUI::AddRow(std::uint32_t section, Row row)
    {
        Section* found = FindSection(section);
        if (found == nullptr)
        {
            return nullptr;
        }
        Row& added = found->rows.emplace_back(std::move(row));
        added.id = m_nextId++;
        BuildRow(added, found->body);
        return &added;
    }

    void DemoUI::BuildRow(Row& row, Fenrir::ElementId body)
    {
        Fenrir::UIRoot& root = *m_root;
        switch (row.kind)
        {
        case RowKind::Text:
            row.element = root.Create(body, "label");
            root.AddClass(row.element, "demo-text");
            row.textElement = row.element;
            break;
        case RowKind::Readout: {
            // Two columns, the label's and the value's, which demo.css sizes.
            row.element = root.Create(body, "panel");
            root.AddClass(row.element, "demo-readout");
            const Fenrir::ElementId label = root.Create(row.element, "label");
            root.AddClass(label, "demo-readout-label");
            root.SetText(label, row.label);
            row.textElement = root.Create(row.element, "label");
            root.AddClass(row.textElement, "demo-readout-value");
            break;
        }
        case RowKind::Separator:
            row.element = root.Create(body, "label");
            root.AddClass(row.element, "demo-separator");
            row.textElement = row.element;
            break;
        case RowKind::Switch:
            // The switch makes its label and controls; the row shows no text of its own.
            row.element = Make(root, body, "panel", "demo-row");
            row.control->Build(root, row.element, row.label);
            ApplyRow(row);
            return;
        }
        root.SetText(row.textElement, row.text);
        ApplyRow(row);
    }

    DemoRow DemoUI::AddSwitch(std::uint32_t section, std::string label, std::shared_ptr<Detail::DemoSwitch> control,
                              std::string hint)
    {
        const Row* row = AddRow(section, {.kind = RowKind::Switch,
                                          .label = std::move(label),
                                          .control = std::move(control),
                                          .hint = std::move(hint)});
        return row != nullptr ? DemoRow(this, row->id) : DemoRow();
    }

    void DemoUI::SetRowText(Row& row, std::string_view text) const
    {
        // A scene may set a readout every frame: an unchanged value leaves the element, and the layout, alone.
        if (row.text == text)
        {
            return;
        }
        row.text = text;
        m_root->SetText(row.textElement, row.text);
    }

    void DemoUI::BuildSceneButtons(const Section& section)
    {
        Fenrir::UIRoot& root = *m_root;
        const Fenrir::ElementId scenes = root.Create(section.body, "panel");
        root.AddClass(scenes, "demo-scenes");
        m_sceneButtons.clear();
        for (std::size_t i = 0; i < m_sceneNames.size(); ++i)
        {
            const Fenrir::ElementId button = root.Create(scenes, "button");
            root.AddClass(button, "demo-scene-button");
            root.SetText(button, m_sceneNames[i]);
            root.AddListener(button, Fenrir::UIEventKind::Click, [this, i](Fenrir::UIListenerContext&) {
                if (m_enterScene)
                {
                    m_enterScene(i);
                }
            });
            m_sceneButtons.push_back(button);
        }
        ApplyActiveScene();
    }

    void DemoUI::ApplyActiveScene() const
    {
        for (std::size_t i = 0; i < m_sceneButtons.size(); ++i)
        {
            if (i == m_activeScene)
            {
                m_root->AddClass(m_sceneButtons[i], "active");
            }
            else
            {
                m_root->RemoveClass(m_sceneButtons[i], "active");
            }
        }
    }

    void DemoUI::PullReadouts()
    {
        // By id, since a pull may add or remove rows.
        std::vector<std::uint32_t> pulled;
        for (const Section& section : m_sections)
        {
            for (const Row& row : section.rows)
            {
                if (row.pull)
                {
                    pulled.push_back(row.id);
                }
            }
        }
        for (const std::uint32_t id : pulled)
        {
            const Row* row = FindRow(id);
            if (row == nullptr)
            {
                continue;
            }
            // Copied, so the call outlives a row it removes.
            const std::function<std::string()> pull = row->pull;
            const std::string value = pull();
            if (Row* still = FindRow(id); still != nullptr)
            {
                SetRowText(*still, value);
            }
        }
    }

    void DemoUI::SyncSwitches()
    {
        // By id, each switch held, since a getter may add or remove rows.
        std::vector<std::pair<std::uint32_t, std::shared_ptr<Detail::DemoSwitch>>> switches;
        for (const Section& section : m_sections)
        {
            for (const Row& row : section.rows)
            {
                if (row.control)
                {
                    switches.emplace_back(row.id, row.control);
                }
            }
        }
        for (const auto& [id, control] : switches)
        {
            if (FindRow(id) != nullptr)
            {
                control->Sync(*m_root);
            }
        }
    }

    void DemoUI::UpdateHint()
    {
        if (!m_hintDirty || !m_hint.IsValid())
        {
            return;
        }
        m_hintDirty = false;
        std::string_view hint = HintOf(m_root->GetHovered());
        if (hint.empty())
        {
            hint = HintOf(m_root->GetFocused());
        }
        if (m_root->GetText(m_hint) != hint)
        {
            // Copied: SetText may not be given a view of the text it replaces.
            const std::string line(hint);
            m_root->SetText(m_hint, line);
            // An empty line takes no room at the panel's foot.
            Display(m_hint, !line.empty());
        }
    }

    std::string_view DemoUI::HintOf(Fenrir::ElementId element) const
    {
        // The row the element is in: its nearest ancestor that is a row's element, or a row a script made in a scene
        // slot (a shared Razor component's), whose hint is the text of its child of class demo-row-hint.
        for (Fenrir::ElementId at = element; at.IsValid(); at = m_root->GetParent(at))
        {
            for (const Section& section : m_sections)
            {
                for (const Row& row : section.rows)
                {
                    if (row.element == at)
                    {
                        return row.hint;
                    }
                }
            }
            if (m_root->HasClass(at, "demo-row"))
            {
                for (const Fenrir::ElementId child : m_root->GetChildren(at))
                {
                    if (m_root->HasClass(child, "demo-row-hint"))
                    {
                        return m_root->GetText(child);
                    }
                }
                return {};
            }
        }
        return {};
    }

    void DemoUI::BuildHelp()
    {
        Fenrir::UIRoot& root = *m_root;
        if (!m_help.IsValid())
        {
            return;
        }
        // The help is made whole each time it changes: a scene's entry, or a reloaded document.
        RemoveChildren(root, m_help);
        const Fenrir::ElementId title = root.Create(m_help, "button");
        root.AddClass(title, "demo-help-title");
        root.SetText(title, "Help");
        root.AddListener(title, Fenrir::UIEventKind::Click, [this](Fenrir::UIListenerContext&) {
            m_helpOpen = !m_helpOpen;
            Display(m_helpBody, m_helpOpen);
        });
        m_helpBody = root.Create(m_help, "panel");
        root.AddClass(m_helpBody, "demo-section-body");
        const Fenrir::ElementId heading = root.Create(m_helpBody, "label");
        root.AddClass(heading, "demo-help-heading");
        root.SetText(heading, m_helpHeading);
        for (const DemoKeyRow& keyRow : m_helpRows)
        {
            MakeKeyRow(root, m_helpBody, keyRow, "demo-key-row", "demo-keys", "demo-what");
        }
        Display(m_helpBody, m_helpOpen);
    }

    void DemoUI::ApplyOpen(const Section& section) const
    {
        Display(section.body, section.open);
        if (section.open)
        {
            m_root->RemoveClass(section.element, "demo-closed");
        }
        else
        {
            m_root->AddClass(section.element, "demo-closed");
        }
    }

    void DemoUI::ApplyRow(const Row& row) const
    {
        Display(row.element, row.shown);
        if (row.control)
        {
            row.control->SetDisabled(*m_root, row.disabled);
        }
        if (row.disabled)
        {
            m_root->AddClass(row.element, "demo-disabled");
        }
        else
        {
            m_root->RemoveClass(row.element, "demo-disabled");
        }
    }

    void DemoUI::ApplyVisible() const
    {
        Display(m_kit, m_visible);
    }

    void DemoUI::Display(Fenrir::ElementId element, bool shown) const
    {
        // Inline, so a stylesheet's edit never shows what the kit hid; hidden by display, an element is neither drawn
        // nor hit-tested, and takes no room.
        (void)m_root->SetStyleProperty(element, "display", shown ? "" : "none");
    }

    void DemoUI::LogHelp() const
    {
        if (m_helpHeading.empty() && m_helpRows.empty())
        {
            return;
        }
        Fenrir::ILogger& log = *m_app.Logger();
        log.Info("Help: {}", m_helpHeading);
        for (const DemoKeyRow& row : m_helpRows)
        {
            if (row.keys.empty())
            {
                log.Info("Help: {}", row.what);
            }
            else
            {
                log.Info("Help: {}: {}", row.keys, row.what);
            }
        }
    }

    void DemoUI::PollToggleKey()
    {
        // While the kit has the focus the root takes the keys, and OnKey sees F1 instead.
        if (const auto* keyboard = m_app.TryGetResource<Fenrir::Keyboard>();
            keyboard != nullptr && keyboard->JustPressed(Fenrir::Key::F1))
        {
            SetVisible(!m_visible);
        }
    }

    void DemoUI::OnKey(Fenrir::Key key)
    {
        if (key != Fenrir::Key::F1 && key != Fenrir::Key::Escape)
        {
            return;
        }
        // F1 is a character of no text, but a text entry's keys are all its own, and its Escape gives up its focus.
        if (dynamic_cast<const Fenrir::TextEntry*>(m_root->GetElement(m_root->GetFocused())) != nullptr)
        {
            return;
        }
        if (key == Fenrir::Key::F1)
        {
            SetVisible(!m_visible);
        }
        else if (m_tutorial)
        {
            m_tutorialAction = TutorialAction::Close;
        }
    }

    // The tutorial card.

    void DemoUI::ShowTutorial(DemoTutorial tutorial)
    {
        if (tutorial.steps.empty())
        {
            return;
        }
        m_tutorial = std::move(tutorial);
        m_tutorialAction.reset();
        ShowTutorialStep(0);
    }

    void DemoUI::CloseTutorial()
    {
        if (m_tutorial)
        {
            EndTutorial(false);
        }
    }

    void DemoUI::EndTutorial(bool immediate)
    {
        m_tutorial.reset();
        m_tutorialAction.reset();
        ++m_tutorialChanges;
        RemoveTutorialCard(immediate);
    }

    void DemoUI::ShowTutorialStep(std::size_t step)
    {
        if (!m_tutorial || step >= m_tutorial->steps.size())
        {
            CloseTutorial();
            return;
        }
        m_tutorialStep = step;
        ++m_tutorialChanges;
        BuildTutorial();
    }

    void DemoUI::RemoveTutorialCard(bool immediate)
    {
        if (m_tutorialCard.IsValid())
        {
            (void)m_root->Remove(m_tutorialCard, immediate ? Fenrir::RemoveMode::Immediate : Fenrir::RemoveMode::Outro);
        }
        m_tutorialCard = {};
    }

    void DemoUI::BuildTutorial()
    {
        Fenrir::UIRoot& root = *m_root;
        if (!m_tutorial)
        {
            RemoveTutorialCard(false);
            return;
        }
        if (!m_tutorialArea.IsValid())
        {
            return;
        }
        if (root.GetElement(m_tutorialCard) == nullptr)
        {
            // A new card comes in through its intro; a step changed in a card that shows keeps the card.
            m_tutorialCard = Make(root, m_tutorialArea, "panel", "demo-box");
            root.AddClass(m_tutorialCard, "demo-tutorial");
        }
        else
        {
            RemoveChildren(root, m_tutorialCard);
        }
        const DemoTutorial& tutorial = *m_tutorial;
        const DemoTutorialStep& step = tutorial.steps[m_tutorialStep];
        const bool last = m_tutorialStep + 1 == tutorial.steps.size();

        const Fenrir::ElementId header = Make(root, m_tutorialCard, "panel", "demo-tutorial-header");
        MakeLabel(root, header, "demo-tutorial-title", tutorial.title);
        MakeLabel(root, header, "demo-tutorial-progress",
                  std::format("{}/{}", m_tutorialStep + 1, tutorial.steps.size()));
        MakeLabel(root, m_tutorialCard, "demo-tutorial-step-title", step.title);
        MakeLabel(root, m_tutorialCard, "demo-tutorial-text", step.text);
        for (const DemoKeyRow& keyRow : step.keys)
        {
            MakeKeyRow(root, m_tutorialCard, keyRow, "demo-tutorial-key-row", "demo-tutorial-key",
                       "demo-tutorial-what");
        }

        // The buttons' clicks are applied in the build stage work, which makes the card's elements again.
        const Fenrir::ElementId buttons = Make(root, m_tutorialCard, "panel", "demo-tutorial-buttons");
        const auto button = [&](std::string_view name, std::string_view caption, TutorialAction action) {
            const Fenrir::ElementId made = Make(root, buttons, "button", name);
            root.AddClass(made, "demo-tutorial-button");
            root.SetText(made, caption);
            root.AddListener(made, Fenrir::UIEventKind::Click,
                             [this, action](Fenrir::UIListenerContext&) { m_tutorialAction = action; });
            return made;
        };
        const Fenrir::ElementId back = button("demo-tutorial-back", "Back", TutorialAction::Back);
        if (auto* control = As<Fenrir::Control>(root, back); control != nullptr)
        {
            control->SetDisabled(m_tutorialStep == 0);
        }
        button("demo-tutorial-next", last ? "Done" : "Next", TutorialAction::Next);
        button("demo-tutorial-close", "Close", TutorialAction::Close);
    }

    void DemoUI::ApplyTutorialInput()
    {
        if (!m_tutorial)
        {
            return;
        }
        // While the kit has no focus the game's keyboard sees Escape; while it has, OnKey does.
        if (const auto* keyboard = m_app.TryGetResource<Fenrir::Keyboard>();
            keyboard != nullptr && keyboard->JustPressed(Fenrir::Key::Escape))
        {
            m_tutorialAction = TutorialAction::Close;
        }
        if (!m_tutorialAction)
        {
            return;
        }
        const TutorialAction action = *m_tutorialAction;
        m_tutorialAction.reset();
        // A click on the card's button took the focus, and the keyboard with it: a step's keys are the scene's.
        for (Fenrir::ElementId at = m_root->GetFocused(); at.IsValid(); at = m_root->GetParent(at))
        {
            if (at == m_tutorialCard)
            {
                m_root->Blur();
                break;
            }
        }
        switch (action)
        {
        case TutorialAction::Back:
            if (m_tutorialStep > 0)
            {
                ShowTutorialStep(m_tutorialStep - 1);
            }
            break;
        case TutorialAction::Next:
            ShowTutorialStep(m_tutorialStep + 1);
            break;
        case TutorialAction::Close:
            CloseTutorial();
            break;
        }
    }

    void DemoUI::AskTutorialDone()
    {
        if (!m_tutorial || !m_tutorial->steps[m_tutorialStep].done)
        {
            return;
        }
        // Copied, and the changes counted: `done` may close the tutorial, show another or leave the scene.
        const std::function<bool(Fenrir::App&)> done = m_tutorial->steps[m_tutorialStep].done;
        const std::uint32_t changes = m_tutorialChanges;
        if (done(m_app) && changes == m_tutorialChanges)
        {
            ShowTutorialStep(m_tutorialStep + 1);
        }
    }

    DemoUI::Section* DemoUI::FindSection(std::uint32_t id)
    {
        const auto found = std::find_if(m_sections.begin(), m_sections.end(),
                                        [id](const Section& section) { return section.id == id; });
        return found != m_sections.end() ? &*found : nullptr;
    }

    DemoUI::Row* DemoUI::FindRow(std::uint32_t id)
    {
        for (Section& section : m_sections)
        {
            for (Row& row : section.rows)
            {
                if (row.id == id)
                {
                    return &row;
                }
            }
        }
        return nullptr;
    }
} // namespace FenrirDemo
