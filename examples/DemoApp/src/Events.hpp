#pragma once

struct FrameBufferResizeEvent
{
    int width, height;
};

struct WindowResizeEvent
{
    int width, height;
};

struct WindowCloseEvent
{
};

struct MouseMoveEvent
{
    double x, y;
};

struct MouseScrollEvent
{
    double xOffset, yOffset;
};

enum class MouseButton
{
    Left = 0,
    Right = 1,
    Middle = 2
};

enum class InputState
{
    Released = 0,
    Pressed = 1,
    Held = 2,
};

struct MouseButtonEvent
{
    MouseButton button;
    InputState state;
    int mods;
};

struct KeyboardKeyEvent
{
    int key;
    int scancode;
    int repeat;
    InputState state;
    int mods;
};