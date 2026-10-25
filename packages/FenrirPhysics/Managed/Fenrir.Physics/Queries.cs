namespace Fenrir.Physics;

// What PhysicsWorld's queries take and give is generated (Generated/): QueryFilter, the hits and Contact from the
// binding's own description of them as C# holds them (Scripting/src/ScriptQueries.hpp, each entity a 32-bit Entity),
// and checked against its native layout whenever a script assembly loads; the box a body's bounds fill is Fenrir.Math's
// Bounds, the math binding's. This file holds what is not shared by layout: the debug-draw categories, which cross as a
// uint.

/// <summary>Which categories of debug lines a physics world draws into its scene's debug drawing. Bit flags.</summary>
[Flags]
public enum PhysicsDebugCategories : uint
{
    None = 0,
    /// <summary>Each collider's wireframe at its body's pose, coloured by motion type.</summary>
    Colliders = 1u << 0,
    /// <summary>Each contact between solid bodies.</summary>
    Contacts = 1u << 1,
    /// <summary>A line from each moving body's position along its linear velocity.</summary>
    Velocities = 1u << 2,
    /// <summary>Each body's world-space bounding box.</summary>
    Bounds = 1u << 3,
    /// <summary>Each joint's two frames as axes: frameA long (red, green, blue), frameB short (pink, yellow,
    /// cyan). They coincide at the rest pose and while the joint holds.</summary>
    Joints = 1u << 4,
    /// <summary>Each active character's shape in use (the crouched one while a stand-up is blocked), its ground
    /// normal coloured by ground state, and its moved velocity.</summary>
    Characters = 1u << 5,
    /// <summary>Each wheel of each active, enabled vehicle: its suspension, the wheel with one spoke, and its contact
    /// coloured by grip (gripping, past its tyre's peak slip, or past its slide slip).</summary>
    Vehicles = 1u << 6,
    /// <summary>Every category.</summary>
    All = Colliders | Contacts | Velocities | Bounds | Joints | Characters | Vehicles,
}
