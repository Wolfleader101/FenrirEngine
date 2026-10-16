#pragma once

#include "FenrirMath/Math.hpp"

struct WindowFrameBufferResizeEvent
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
    double x, y;
};

struct KeyboardKeyEvent
{
    int key;
    int scancode;
    int repeat;
    InputState state;
    int mods;
};

// A left click in the editor's scene view, as the ray from the camera through the clicked point (the editor sends it).
struct SceneClickEvent
{
    Fenrir::Math::Vec3 origin;
    Fenrir::Math::Vec3 direction; // Unit length.
};
