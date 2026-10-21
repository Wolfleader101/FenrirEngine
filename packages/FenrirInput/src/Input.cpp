#include "FenrirInput/Input.hpp"

#include "FenrirApp/App.hpp"

namespace Fenrir
{
    void Input::Install(App& app)
    {
        app.AddResource<Keyboard>();
        app.AddResource<Mouse>();
    }

    void InputFeed::BeginFrame(App& app)
    {
        if (Keyboard* keyboard = app.GetResource<Keyboard>())
        {
            keyboard->pressed.fill(false);
        }
        if (Mouse* mouse = app.GetResource<Mouse>())
        {
            mouse->deltaX = 0.0f;
            mouse->deltaY = 0.0f;
        }
    }

    void InputFeed::SetKey(App& app, Key key, bool down)
    {
        Keyboard* keyboard = app.GetResource<Keyboard>();
        if (keyboard == nullptr || key == Key::Unknown)
        {
            return;
        }
        const auto index = static_cast<std::size_t>(key);
        if (down && !keyboard->down[index])
        {
            keyboard->pressed[index] = true;
        }
        keyboard->down[index] = down;
    }

    void InputFeed::MouseMoved(App& app, float x, float y, float dx, float dy)
    {
        if (Mouse* mouse = app.GetResource<Mouse>())
        {
            mouse->x = x;
            mouse->y = y;
            mouse->deltaX += dx;
            mouse->deltaY += dy;
        }
    }

    void InputFeed::SetMouseButton(App& app, int button, bool down)
    {
        Mouse* mouse = app.GetResource<Mouse>();
        if (mouse != nullptr && button >= 0 && button < 3)
        {
            mouse->buttons[static_cast<std::size_t>(button)] = down;
        }
    }
} // namespace Fenrir
