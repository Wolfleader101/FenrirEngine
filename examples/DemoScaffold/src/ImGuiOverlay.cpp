#include "ImGuiOverlay.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <imgui.h>

#include "FenrirApp/App.hpp"
#include "FenrirAssets/AssetStore.hpp"
#include "FenrirDemoScaffold/DemoScaffold.hpp"
#include "FenrirDraw2D/DrawList2D.hpp"
#include "FenrirInput/Input.hpp"
#include "FenrirLogger/ILogger.hpp"
#include "FenrirRenderer/Overlay.hpp"
#include "FenrirWindow/Window.hpp"

namespace FenrirDemo::Detail
{
    namespace
    {
        // The null backend draws as if into a window of this logical size (Renderer's picking says so), and ImGui
        // lays out against it when there is no window.
        constexpr Fenrir::WindowSize NoWindowSize{1280, 720};

        // ImGui's vertices are what builtin:2d reads.
        static_assert(sizeof(ImDrawVert) == sizeof(Fenrir::Vertex2D));
        static_assert(offsetof(ImDrawVert, pos) == 0 && offsetof(ImDrawVert, uv) == 8 &&
                      offsetof(ImDrawVert, col) == 16);

        ImGuiKey ImGuiKeyOf(Fenrir::Key key)
        {
            using Fenrir::Key;
            const auto code = static_cast<std::uint16_t>(key);
            // Letters, digits and F1 to F24 run in order in both enums.
            if (key >= Key::A && key <= Key::Z)
            {
                return static_cast<ImGuiKey>(ImGuiKey_A + (code - static_cast<std::uint16_t>(Key::A)));
            }
            if (key >= Key::Num0 && key <= Key::Num9)
            {
                return static_cast<ImGuiKey>(ImGuiKey_0 + (code - static_cast<std::uint16_t>(Key::Num0)));
            }
            if (key >= Key::F1 && key <= Key::F24)
            {
                return static_cast<ImGuiKey>(ImGuiKey_F1 + (code - static_cast<std::uint16_t>(Key::F1)));
            }
            if (key >= Key::Keypad0 && key <= Key::Keypad9)
            {
                return static_cast<ImGuiKey>(ImGuiKey_Keypad0 + (code - static_cast<std::uint16_t>(Key::Keypad0)));
            }
            switch (key)
            {
            case Key::Escape:
                return ImGuiKey_Escape;
            case Key::Enter:
                return ImGuiKey_Enter;
            case Key::Tab:
                return ImGuiKey_Tab;
            case Key::Backspace:
                return ImGuiKey_Backspace;
            case Key::Space:
                return ImGuiKey_Space;
            case Key::Minus:
                return ImGuiKey_Minus;
            case Key::Equals:
                return ImGuiKey_Equal;
            case Key::LeftBracket:
                return ImGuiKey_LeftBracket;
            case Key::RightBracket:
                return ImGuiKey_RightBracket;
            case Key::Backslash:
                return ImGuiKey_Backslash;
            case Key::Semicolon:
                return ImGuiKey_Semicolon;
            case Key::Apostrophe:
                return ImGuiKey_Apostrophe;
            case Key::Grave:
                return ImGuiKey_GraveAccent;
            case Key::Comma:
                return ImGuiKey_Comma;
            case Key::Period:
                return ImGuiKey_Period;
            case Key::Slash:
                return ImGuiKey_Slash;
            case Key::CapsLock:
                return ImGuiKey_CapsLock;
            case Key::PrintScreen:
                return ImGuiKey_PrintScreen;
            case Key::ScrollLock:
                return ImGuiKey_ScrollLock;
            case Key::Pause:
                return ImGuiKey_Pause;
            case Key::Insert:
                return ImGuiKey_Insert;
            case Key::Delete:
                return ImGuiKey_Delete;
            case Key::Home:
                return ImGuiKey_Home;
            case Key::End:
                return ImGuiKey_End;
            case Key::PageUp:
                return ImGuiKey_PageUp;
            case Key::PageDown:
                return ImGuiKey_PageDown;
            case Key::Left:
                return ImGuiKey_LeftArrow;
            case Key::Right:
                return ImGuiKey_RightArrow;
            case Key::Up:
                return ImGuiKey_UpArrow;
            case Key::Down:
                return ImGuiKey_DownArrow;
            case Key::NumLock:
                return ImGuiKey_NumLock;
            case Key::KeypadDivide:
                return ImGuiKey_KeypadDivide;
            case Key::KeypadMultiply:
                return ImGuiKey_KeypadMultiply;
            case Key::KeypadMinus:
                return ImGuiKey_KeypadSubtract;
            case Key::KeypadPlus:
                return ImGuiKey_KeypadAdd;
            case Key::KeypadEnter:
                return ImGuiKey_KeypadEnter;
            case Key::KeypadPeriod:
                return ImGuiKey_KeypadDecimal;
            case Key::Menu:
                return ImGuiKey_Menu;
            case Key::LeftCtrl:
                return ImGuiKey_LeftCtrl;
            case Key::LeftShift:
                return ImGuiKey_LeftShift;
            case Key::LeftAlt:
                return ImGuiKey_LeftAlt;
            case Key::LeftSuper:
                return ImGuiKey_LeftSuper;
            case Key::RightCtrl:
                return ImGuiKey_RightCtrl;
            case Key::RightShift:
                return ImGuiKey_RightShift;
            case Key::RightAlt:
                return ImGuiKey_RightAlt;
            case Key::RightSuper:
                return ImGuiKey_RightSuper;
            default:
                return ImGuiKey_None;
            }
        }

        Fenrir::CursorShape CursorShapeOf(ImGuiMouseCursor cursor)
        {
            switch (cursor)
            {
            case ImGuiMouseCursor_None:
                return Fenrir::CursorShape::Hidden;
            case ImGuiMouseCursor_TextInput:
                return Fenrir::CursorShape::Text;
            case ImGuiMouseCursor_ResizeNS:
                return Fenrir::CursorShape::ResizeNorthSouth;
            case ImGuiMouseCursor_ResizeEW:
                return Fenrir::CursorShape::ResizeEastWest;
            case ImGuiMouseCursor_ResizeNESW:
                return Fenrir::CursorShape::ResizeNorthEastSouthWest;
            case ImGuiMouseCursor_ResizeNWSE:
                return Fenrir::CursorShape::ResizeNorthWestSouthEast;
            case ImGuiMouseCursor_Hand:
                return Fenrir::CursorShape::Hand;
            case ImGuiMouseCursor_NotAllowed:
                return Fenrir::CursorShape::NotAllowed;
            // FenrirWindow has no move or busy cursor.
            default:
                return Fenrir::CursorShape::Arrow;
            }
        }

        // ImGui hands its buffers over as a pointer and a count, so the bounds are only as good as ImGui's.
        template <typename T>
        std::span<const T> SpanOf(const T* data, int count)
        {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunsafe-buffer-usage"
#endif
            return std::span<const T>(data, static_cast<std::size_t>(count));
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
        }

        // An image asset handle as an ImTextureID: never zero (ImTextureID_Invalid), since a handle's generation is
        // not zero.
        ImTextureID TextureIdOf(Fenrir::AssetHandle<Fenrir::Image> image)
        {
            return (static_cast<ImTextureID>(image.generation) << 32u) | image.index;
        }

        Fenrir::AssetHandle<Fenrir::Image> ImageOf(ImTextureID id)
        {
            Fenrir::AssetHandle<Fenrir::Image> image;
            image.index = static_cast<std::uint32_t>(id & 0xFFFFFFFFu);
            image.generation = static_cast<std::uint32_t>(id >> 32u);
            return image;
        }
    } // namespace

    struct ImGuiOverlayState
    {
        Fenrir::App* app = nullptr;
        Fenrir::AssetStore* store = nullptr;
        ImGuiContext* context = nullptr;
        Fenrir::InputRouterId router;
        Fenrir::AssetHandle<Fenrir::Shader> shader;

        // The mouse buttons whose presses ImGui took: their releases come to it alone (ADR-0027's control owners).
        std::array<bool, Fenrir::MouseButtonCount> ownedButtons{};
        // Where ImGui last said text is typed, for the IME.
        std::optional<Fenrir::CaretRect> caret;
        // The clipboard text ImGui last asked for, kept alive for it.
        std::string clipboard;
        std::optional<ImGuiMouseCursor> cursor;
        bool inFrame = false;

        [[nodiscard]] static ImGuiOverlayState& Of()
        {
            return *static_cast<ImGuiOverlayState*>(ImGui::GetIO().BackendPlatformUserData);
        }

        [[nodiscard]] bool OwnsMouse() const
        {
            return std::any_of(ownedButtons.begin(), ownedButtons.end(), [](bool owned) { return owned; });
        }

        // Whether the game holds a mouse button: a drag of its own (the free-fly camera's look), which ImGui stays out
        // of, seeing neither the pointer nor the wheel.
        [[nodiscard]] bool GameHoldsMouse() const
        {
            const auto& mouse = app->GetResource<Fenrir::Mouse>();
            for (std::size_t i = 0; i < Fenrir::MouseButtonCount; ++i)
            {
                if (mouse.IsDown(static_cast<Fenrir::MouseButton>(i)))
                {
                    return true;
                }
            }
            return false;
        }

        Fenrir::RouteResult Route(const Fenrir::RoutedInput& input)
        {
            ImGui::SetCurrentContext(context);
            return std::visit([this](const auto& event) { return RouteEvent(event); }, input);
        }

        Fenrir::RouteResult RouteEvent(const Fenrir::KeyEvent& event)
        {
            if (event.window != Fenrir::Window::MainWindow())
            {
                return Fenrir::RouteResult::Passed;
            }
            // A repeat or a release reaches the router only for a press it took (or a cancellation of one).
            const bool press = event.pressed && !event.repeat && !event.cancelled;
            if (press && !ImGui::GetIO().WantCaptureKeyboard)
            {
                return Fenrir::RouteResult::Passed;
            }
            ImGuiIO& io = ImGui::GetIO();
            io.AddKeyEvent(ImGuiMod_Ctrl, Fenrir::HasModifiers(event.modifiers, Fenrir::KeyModifiers::Ctrl));
            io.AddKeyEvent(ImGuiMod_Shift, Fenrir::HasModifiers(event.modifiers, Fenrir::KeyModifiers::Shift));
            io.AddKeyEvent(ImGuiMod_Alt, Fenrir::HasModifiers(event.modifiers, Fenrir::KeyModifiers::Alt));
            io.AddKeyEvent(ImGuiMod_Super, Fenrir::HasModifiers(event.modifiers, Fenrir::KeyModifiers::Super));
            if (const ImGuiKey key = ImGuiKeyOf(event.key); key != ImGuiKey_None)
            {
                io.AddKeyEvent(key, event.pressed && !event.cancelled);
            }
            return Fenrir::RouteResult::Consumed;
        }

        Fenrir::RouteResult RouteEvent(const Fenrir::TextEvent& event)
        {
            if (event.window != Fenrir::Window::MainWindow() || !ImGui::GetIO().WantTextInput)
            {
                return Fenrir::RouteResult::Passed;
            }
            ImGui::GetIO().AddInputCharacter(static_cast<unsigned int>(event.codepoint));
            return Fenrir::RouteResult::Consumed;
        }

        Fenrir::RouteResult RouteEvent(const Fenrir::MouseButtonEvent& event)
        {
            if (event.window != Fenrir::Window::MainWindow())
            {
                return Fenrir::RouteResult::Passed;
            }
            ImGuiIO& io = ImGui::GetIO();
            const auto button = static_cast<std::size_t>(event.button);
            if (!event.pressed || event.cancelled)
            {
                // The release of a press ImGui took.
                ownedButtons[button] = false;
                io.AddMouseButtonEvent(static_cast<int>(button), false);
                return Fenrir::RouteResult::Consumed;
            }
            if (io.WantCaptureMouse && !GameHoldsMouse())
            {
                ownedButtons[button] = true;
                io.AddMouseButtonEvent(static_cast<int>(button), true);
                return Fenrir::RouteResult::Consumed;
            }
            // The game's press. ImGui sees a click away from its windows, so a text field that had the keyboard lets
            // go of it, as a click anywhere else does; the release goes to the game, so ImGui gets its own at once.
            io.AddMouseButtonEvent(static_cast<int>(button), true);
            io.AddMouseButtonEvent(static_cast<int>(button), false);
            return Fenrir::RouteResult::Passed;
        }

        Fenrir::RouteResult RouteEvent(const Fenrir::MouseMotionEvent& event)
        {
            if (event.window != Fenrir::Window::MainWindow() || GameHoldsMouse())
            {
                return Fenrir::RouteResult::Passed;
            }
            ImGui::GetIO().AddMousePosEvent(event.position.x, event.position.y);
            // Hovering passes (the pointer claim says it is over ImGui); a drag ImGui began is its alone.
            return OwnsMouse() ? Fenrir::RouteResult::Consumed : Fenrir::RouteResult::Passed;
        }

        Fenrir::RouteResult RouteEvent(const Fenrir::MouseScrollEvent& event)
        {
            ImGuiIO& io = ImGui::GetIO();
            if (event.window != Fenrir::Window::MainWindow() || !io.WantCaptureMouse || GameHoldsMouse())
            {
                return Fenrir::RouteResult::Passed;
            }
            io.AddMouseWheelEvent(event.notches.x, event.notches.y);
            return Fenrir::RouteResult::Consumed;
        }

        Fenrir::RouteResult RouteEvent(const Fenrir::WindowFocusEvent& event)
        {
            if (event.window == Fenrir::Window::MainWindow())
            {
                ImGui::GetIO().AddFocusEvent(event.focused);
            }
            return Fenrir::RouteResult::Passed;
        }

        // Gamepads are the game's: ImGui's gamepad navigation is off.
        template <typename Event>
        Fenrir::RouteResult RouteEvent(const Event&)
        {
            return Fenrir::RouteResult::Passed;
        }

        void PublishClaims()
        {
            const ImGuiIO& io = ImGui::GetIO();
            Fenrir::WindowInputClaims claims;
            claims.pointer = io.WantCaptureMouse;
            claims.pointerCapture = OwnsMouse();
            claims.keyboardFocus = io.WantCaptureKeyboard;
            claims.textInput = io.WantTextInput;
            if (claims.textInput && caret)
            {
                claims.caret = *caret;
            }
            Fenrir::Input::SetInputClaims(*app, router, Fenrir::Window::MainWindow(), claims);
        }

        void UpdateCursor()
        {
            const Fenrir::WindowId window = Fenrir::Window::MainWindow();
            // The free-fly camera's look hides and holds the cursor; ImGui leaves it alone then.
            if (!Fenrir::Window::Exists(*app, window) ||
                Fenrir::Window::GetCursorMode(*app, window) != Fenrir::CursorMode::Normal)
            {
                cursor.reset();
                return;
            }
            const ImGuiMouseCursor wanted = ImGui::GetMouseCursor();
            if (cursor != wanted)
            {
                cursor = wanted;
                (void)Fenrir::Window::SetCursorShape(*app, window, CursorShapeOf(wanted));
            }
        }

        // ImGui's textures (ADR-0027: the font atlas re-uploaded whole when it changes) as app-owned image assets.
        void UpdateTextures(ImVector<ImTextureData*>& textures)
        {
            for (ImTextureData* texture : textures)
            {
                if (texture->Status == ImTextureStatus_WantCreate || texture->Status == ImTextureStatus_WantUpdates)
                {
                    Fenrir::ImageData image;
                    image.width = static_cast<std::uint32_t>(texture->Width);
                    image.height = static_cast<std::uint32_t>(texture->Height);
                    image.format = Fenrir::ImageFormat::RGBA8;
                    image.colourSpace = Fenrir::ColourSpace::Srgb;
                    const std::span<const unsigned char> pixels =
                        SpanOf(static_cast<const unsigned char*>(texture->GetPixels()), texture->GetSizeInBytes());
                    image.pixels.resize(pixels.size());
                    std::transform(pixels.begin(), pixels.end(), image.pixels.begin(),
                                   [](unsigned char value) { return static_cast<std::byte>(value); });
                    if (texture->Status == ImTextureStatus_WantCreate)
                    {
                        texture->SetTexID(
                            TextureIdOf(store->Add<Fenrir::Image>(std::move(image), Fenrir::AssetOwner::App())));
                    }
                    else
                    {
                        (void)store->Replace<Fenrir::Image>(ImageOf(texture->TexID), std::move(image));
                    }
                    texture->SetStatus(ImTextureStatus_OK);
                }
                else if (texture->Status == ImTextureStatus_WantDestroy && texture->UnusedFrames > 0)
                {
                    DestroyTexture(*texture);
                }
            }
        }

        void DestroyTexture(ImTextureData& texture)
        {
            if (texture.TexID != ImTextureID_Invalid)
            {
                (void)store->Unload<Fenrir::Image>(ImageOf(texture.TexID));
            }
            texture.SetTexID(ImTextureID_Invalid);
            texture.SetStatus(ImTextureStatus_Destroyed);
        }

        // ImGui's draw data as one draw list: every ImDrawList's vertices and indices, one after another, and a
        // command per ImDrawCmd, its clip rectangle the scissor.
        [[nodiscard]] Fenrir::DrawList2D DrawListOf(const ImDrawData& data) const
        {
            Fenrir::DrawList2D list;
            Fenrir::PipelineDescription2D pipeline;
            pipeline.shader = shader;
            pipeline.vertexLayout = Fenrir::Vertex2DLayout();
            pipeline.indexType = sizeof(ImDrawIdx) == 2 ? Fenrir::IndexType2D::UInt16 : Fenrir::IndexType2D::UInt32;
            pipeline.blendMode = Fenrir::BlendMode2D::AlphaBlend;
            pipeline.inputColourSpace = Fenrir::ColourSpace::Srgb;
            pipeline.blendSpace = Fenrir::BlendSpace2D::Linear;
            const std::uint32_t pipelineIndex = list.AddPipeline(pipeline);

            for (const ImDrawList* drawList : data.CmdLists)
            {
                const std::uint32_t firstVertex =
                    list.AppendVertices(SpanOf<ImDrawVert>(drawList->VtxBuffer.Data, drawList->VtxBuffer.Size));
                const std::uint32_t firstIndex =
                    list.AppendIndices(SpanOf<ImDrawIdx>(drawList->IdxBuffer.Data, drawList->IdxBuffer.Size));
                for (const ImDrawCmd& command : drawList->CmdBuffer)
                {
                    // No widget of ImGui's own draws through a callback.
                    if (command.UserCallback != nullptr || command.ElemCount == 0)
                    {
                        continue;
                    }
                    const float left = command.ClipRect.x - data.DisplayPos.x;
                    const float top = command.ClipRect.y - data.DisplayPos.y;
                    const float right = command.ClipRect.z - data.DisplayPos.x;
                    const float bottom = command.ClipRect.w - data.DisplayPos.y;
                    if (right <= left || bottom <= top)
                    {
                        continue;
                    }
                    Fenrir::Command2D draw;
                    draw.pipeline = pipelineIndex;
                    draw.textures.push_back({ImageOf(command.GetTexID())});
                    draw.scissor = Fenrir::Rect2D{left, top, right - left, bottom - top};
                    draw.indexOffset = firstIndex + command.IdxOffset;
                    draw.indexCount = command.ElemCount;
                    draw.baseVertex = static_cast<std::int32_t>(firstVertex + command.VtxOffset);
                    list.commands.push_back(std::move(draw));
                }
            }
            return list;
        }
    };

    namespace
    {
        /** @brief The overlay's input router: hands each input to the overlay while it lives. */
        class ImGuiRouter final : public Fenrir::InputRouter
        {
          public:
            explicit ImGuiRouter(std::weak_ptr<ImGuiOverlayState> state) : m_state(std::move(state))
            {
            }

            Fenrir::RouteResult Route(Fenrir::App&, const Fenrir::RoutedInput& input) override
            {
                const std::shared_ptr<ImGuiOverlayState> state = m_state.lock();
                return state != nullptr ? state->Route(input) : Fenrir::RouteResult::Passed;
            }

          private:
            std::weak_ptr<ImGuiOverlayState> m_state;
        };
    } // namespace

    ImGuiOverlay::ImGuiOverlay(Fenrir::App& app, Fenrir::AssetStore& store)
        : m_state(std::make_shared<ImGuiOverlayState>())
    {
        ImGuiOverlayState& state = *m_state;
        state.app = &app;
        state.store = &store;
        state.context = ImGui::CreateContext();
        ImGui::SetCurrentContext(state.context);

        ImGuiIO& io = ImGui::GetIO();
        // Demo panels are laid out by their scenes each run, so nothing is saved between runs.
        io.IniFilename = nullptr;
        io.BackendPlatformName = "FenrirDemoScaffold";
        io.BackendRendererName = "FenrirDraw2D";
        io.BackendPlatformUserData = &state;
        io.BackendRendererUserData = &state;
        io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors | ImGuiBackendFlags_RendererHasVtxOffset |
                           ImGuiBackendFlags_RendererHasTextures;

        ImGuiPlatformIO& platform = ImGui::GetPlatformIO();
        platform.Platform_GetClipboardTextFn = [](ImGuiContext*) -> const char* {
            ImGuiOverlayState& self = ImGuiOverlayState::Of();
            self.clipboard = Fenrir::Window::ClipboardText(*self.app);
            return self.clipboard.c_str();
        };
        platform.Platform_SetClipboardTextFn = [](ImGuiContext*, const char* text) {
            (void)Fenrir::Window::SetClipboardText(*ImGuiOverlayState::Of().app, text);
        };
        // Called when what ImGui wants of the IME changes; the claims carry it to FenrirWindow.
        platform.Platform_SetImeDataFn = [](ImGuiContext*, ImGuiViewport*, ImGuiPlatformImeData* data) {
            ImGuiOverlayState& self = ImGuiOverlayState::Of();
            if (data->WantVisible || data->WantTextInput)
            {
                self.caret = Fenrir::CaretRect{{data->InputPos.x, data->InputPos.y}, {1.0f, data->InputLineHeight}};
            }
            else
            {
                self.caret.reset();
            }
        };

        Fenrir::LoadResult<Fenrir::Shader> shader = store.Load<Fenrir::Shader>("builtin:2d", Fenrir::AssetOwner::App());
        if (!shader.Ok())
        {
            app.Logger()->Error("ImGuiOverlay: builtin:2d is not there, so ImGui draws nothing");
        }
        state.shader = shader.handle;
        state.router =
            Fenrir::Input::AddInputRouter(app, ImGuiOverlayRouterPriority, std::make_shared<ImGuiRouter>(m_state));
    }

    ImGuiOverlay::ImGuiOverlay(ImGuiOverlay&& other) noexcept = default;
    ImGuiOverlay& ImGuiOverlay::operator=(ImGuiOverlay&& other) noexcept = default;

    ImGuiOverlay::~ImGuiOverlay()
    {
        if (m_state == nullptr)
        {
            return;
        }
        ImGuiOverlayState& state = *m_state;
        Fenrir::Input::RemoveInputRouter(*state.app, state.router);
        ImGui::SetCurrentContext(state.context);
        if (state.inFrame)
        {
            ImGui::EndFrame();
        }
        for (ImTextureData* texture : ImGui::GetPlatformIO().Textures)
        {
            if (texture->Status != ImTextureStatus_Destroyed)
            {
                state.DestroyTexture(*texture);
            }
        }
        ImGuiIO& io = ImGui::GetIO();
        io.BackendPlatformUserData = nullptr;
        io.BackendRendererUserData = nullptr;
        ImGui::DestroyContext(state.context);
    }

    void ImGuiOverlay::BeginFrame()
    {
        ImGuiOverlayState& state = *m_state;
        Fenrir::App& app = *state.app;
        ImGui::SetCurrentContext(state.context);
        if (state.inFrame)
        {
            ImGui::EndFrame();
        }

        const Fenrir::WindowId window = Fenrir::Window::MainWindow();
        const bool hasWindow = Fenrir::Window::Exists(app, window);
        const Fenrir::WindowSize size = hasWindow ? Fenrir::Window::LogicalSize(app, window) : NoWindowSize;
        // ImGui works in logical units, as the overlay does; its fonts are rasterised at the content scale, so text
        // stays crisp on a scaled display.
        const float scale = hasWindow ? Fenrir::Window::ContentScale(app, window) : 1.0f;
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize =
            ImVec2(static_cast<float>(std::max(size.width, 0)), static_cast<float>(std::max(size.height, 0)));
        io.DisplayFramebufferScale = ImVec2(scale, scale);
        io.DeltaTime = static_cast<float>(std::max(app.GetTime().deltaTime, 1e-4));

        ImGui::NewFrame();
        state.inFrame = true;
        state.PublishClaims();
        state.UpdateCursor();
    }

    void ImGuiOverlay::EndFrame()
    {
        ImGuiOverlayState& state = *m_state;
        if (!state.inFrame)
        {
            return;
        }
        ImGui::SetCurrentContext(state.context);
        ImGui::Render();
        state.inFrame = false;

        ImDrawData* data = ImGui::GetDrawData();
        if (data == nullptr)
        {
            return;
        }
        if (data->Textures != nullptr)
        {
            state.UpdateTextures(*data->Textures);
        }
        auto* overlay = state.app->TryGetResource<Fenrir::Overlay>();
        if (overlay == nullptr || data->TotalVtxCount == 0 || state.shader.IsEmpty())
        {
            return;
        }
        overlay->Submit(Fenrir::Window::MainWindow(), ImGuiOverlayLayer, state.DrawListOf(*data));
    }
} // namespace FenrirDemo::Detail
