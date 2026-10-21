#pragma once

// A physics world's vehicles (ADR-0015): each a backend vehicle on its chassis's body, removed before its body goes,
// made after the joints in the edits, run inside the step by the backend, and written back with the bodies: its wheel
// entities' poses, and their poses between ticks blended in the chassis's axes, are handed to Poses, which writes them.
// Reads the chassis through Bodies' const members and Poses. Knows the ECS, not Jolt: it talks to Backend::World.
// Private to the FenrirPhysics package.

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <vector>

#include "Backend/Backend.hpp"
#include "Bodies.hpp"
#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirECS/TrackedComponents.hpp"
#include "FenrirPhysics/Vehicles.hpp"

namespace Fenrir
{
    class EntityList;
    class ILogger;
} // namespace Fenrir

namespace Fenrir::Detail
{
    class DebugCanvas;
    class Poses;

    class Vehicles
    {
      public:
        /** @brief The vehicles of `entities`, including those already there, made in `backend` on the bodies of
         * `bodies`, whose poses, and their wheels', `poses` keeps and writes. */
        Vehicles(EntityList& entities, ILogger& logger, Backend::World& backend, const Bodies& bodies, Poses& poses);

        /** @brief Drains the Vehicle, VehicleWheel, VehicleDifferential and VehicleAntiRollBar tracked sets, after the
         * bodies' plan: a vehicle whose Vehicle or one of whose wheels came or went since the last tick loses its
         * backend vehicle, made again in the edits. */
        void Drain();

        /** @brief Removes the backend vehicles on any of `bodies`, which are about to be destroyed. */
        void DropOn(std::span<const Backend::BodyId> bodies);

        /** @brief Edits, after the joints, in chassis entity order: warns about vehicles and wheels gone inactive,
         * makes the backend vehicle of each enabled chassis with a dynamic body and wheels, remade when its body or
         * wheel set changed, and gives it its differentials, anti-roll bars and driver input, waking the chassis on a
         * change. */
        void Sync();

        /** @brief Write-back, before Poses writes, for a step of `dt` seconds: each vehicle's state, its wheels'
         * states, their poses handed to Poses, and how its wheels are shown between ticks; the states of those with no
         * backend vehicle reset. A sleeping vehicle keeps the state it fell asleep with, its wheels' spin 0. */
        void WriteBack(float dt);

        /** @brief Before Poses' Blend: hands Poses the wheels of each vehicle whose chassis Blend blends, `fraction` of
         * the way between their last two steps in the chassis's axes, on the chassis's blended pose. */
        void BlendWheels(float fraction);

        /** @brief Debug lines: each made vehicle's wheels, their suspension, and their contact points coloured by
         * grip. */
        void Draw(DebugCanvas& canvas) const;

      private:
        // A vehicle's differentials and anti-roll bars, by wheel index, as they are to be given to its backend vehicle.
        struct VehicleLinks
        {
            std::vector<Backend::VehicleDifferentialDesc> differentials;
            std::vector<Backend::VehicleAntiRollBarDesc> antiRollBars;
        };

        // What physics knows of a vehicle, by chassis entity: its backend vehicle, the body and wheel entities it was
        // made with, and the driver input and kind last given to it. None while it is disabled or inactive, or has no
        // wheels.
        struct VehicleRecord
        {
            [[nodiscard]] bool Made() const
            {
                return id.value != Backend::VehicleId{}.value;
            }

            Backend::VehicleId id;
            Backend::BodyId body;
            // In wheel order (entity order), with each wheel's settings as last applied and as given to the backend.
            std::vector<std::uint32_t> wheels;
            std::vector<VehicleWheel> appliedWheels;
            std::vector<Backend::VehicleWheelDesc> given;
            VehicleProbe probe = VehicleProbe::Cylinder;
            float maxTilt = 0.0f;
            // The driver input as game code last wrote it: any change wakes the chassis.
            VehicleInput input;
            // The differentials and anti-roll bars last given to it.
            VehicleLinks links;
            // The kind as game code last gave it, to notice its edits, and as applied, in range.
            VehicleKind kind;
            VehicleKind applied;
            // The kinds of out-of-range value its kind holds and has been warned about: each is warned about once per
            // vehicle until a valid value clears it.
            std::uint32_t warned = 0;
            // Dropped for a rebuild: what the vehicle made next carries over, each wheel's spin by its entity.
            struct Carry
            {
                float engineRpm = 0.0f;
                int gear = 0;
                float clutch = 0.0f;
                std::map<std::uint32_t, Backend::VehicleWheelSpin> wheels;
            };
            std::optional<Carry> carry;
            // Asleep: the read the step it fell asleep left, its wheels still, kept until it wakes.
            std::optional<Backend::VehicleRead> parked;

            // How its wheels are shown between ticks, in the chassis's axes, in wheel order: as the step before last
            // and the last step left them, the spin unwrapped. A new vehicle, or its chassis teleported, snaps: the
            // two are the same after its next step, so nothing blends across the jump.
            struct WheelShown
            {
                float compression = 0.0f;
                float steer = 0.0f;
                float spin = 0.0f;
            };
            std::vector<WheelShown> previous;
            std::vector<WheelShown> current;
            bool snap = true;
        };

        // What physics knows of a VehicleDifferential or VehicleAntiRollBar, by its entity: whether it is inactive,
        // warned about each time it becomes so, and the kinds of out-of-range value already warned about, each once.
        struct VehicleLinkRecord
        {
            bool inactive = false;
            std::uint32_t warned = 0;
            // The entities it names that have been wheels while named, so one that loses its VehicleWheel is a dead
            // name rather than a wrong one; and its dead names, each warned about once.
            std::set<std::uint32_t> wheels;
            std::set<std::uint32_t> dead;
        };

        /** @brief Removes the vehicle's backend vehicle, if it has one, and forgets what was applied to it. For a
         * rebuild (`carryOver`), its engine speed, gear and wheel spins are kept for the vehicle made next, or kept
         * from an earlier rebuild's drop if it has none; otherwise the vehicle made next starts fresh. */
        void DropVehicle(VehicleRecord& record, bool carryOver);
        /** @brief Why the Vehicle on `chassis` is inactive: no made dynamic body with a solid collider, or beside a
         * CharacterBody. Empty if it can be a vehicle. */
        [[nodiscard]] std::string WhyNoChassis(std::uint32_t chassis) const;
        /** @brief Why the VehicleWheel on `wheel` is inactive: naming no other entity of its scene with a Vehicle, or
         * beside a RigidBody, CharacterBody or Collider. Empty if it can be a wheel. */
        [[nodiscard]] std::string WhyNoWheel(std::uint32_t wheel) const;
        /** @brief Warns once about each Vehicle and each VehicleWheel that has gone inactive since the last tick. */
        void WarnAboutInactiveVehicles();
        /** @brief The made dynamic body of the chassis on `chassis`, if it can be a vehicle's; null otherwise. */
        [[nodiscard]] const Bodies::Record* ChassisBodyOf(std::uint32_t chassis) const;
        /** @brief Each vehicle's wheel entities that can be its wheels, in entity order, by chassis entity. */
        [[nodiscard]] std::map<std::uint32_t, std::vector<std::uint32_t>> WheelsByVehicle() const;
        /** @brief The backend's wheel for the wheel on `entity`, its settings corrected (each kind of out-of-range
         * value warned about once per wheel) and its position scaled by `scale`, the chassis's world scale. */
        [[nodiscard]] Backend::VehicleWheelDesc WheelDesc(std::uint32_t entity, const VehicleWheel& wheel,
                                                          const Math::Vec3& scale);
        /** @brief Patches a made vehicle's edited wheels, probe and tilt limit into its backend vehicle; true if
         * anything changed. */
        bool SyncVehicleSettings(VehicleRecord& record, const Vehicle& vehicle, const Math::Vec3& scale);
        /** @brief A wheel's pose relative to its chassis's origin, in the chassis's axes: its position raised up its
         * steering axis by its compression, turned by its camber and toe, steered about its steering axis, then spun
         * about its axle, X. */
        [[nodiscard]] static Transform WheelPose(const Backend::VehicleWheelDesc& wheel,
                                                 const Backend::VehicleWheelRead& read);
        /** @brief Hands Poses the pose of each wheel entity of the vehicle on `chassis` from `read`, in the chassis's
         * axes, for Poses to write against the chassis's step pose. */
        void HandWheels(std::uint32_t chassis, const VehicleRecord& record, const Backend::VehicleRead& read);
        /** @brief After a step of `dt` seconds left the vehicle's wheels as `read`: their last two steps for
         * BlendWheels, the spin unwrapped. A new vehicle, or one whose chassis was `teleported`, snaps. */
        void AdvanceWheels(VehicleRecord& record, const Backend::VehicleRead& read, float dt, bool teleported);
        /** @brief Each vehicle's differentials and anti-roll bars, in entity order, by chassis entity, from `wheelsOf`
         * (WheelsByVehicle): out-of-range values corrected and those naming wrong wheels inactive, each with a
         * warning. */
        [[nodiscard]] std::map<std::uint32_t, VehicleLinks> LinksByVehicle(
            const std::map<std::uint32_t, std::vector<std::uint32_t>>& wheelsOf);

        EntityList* m_entities;
        ILogger* m_logger;
        Backend::World* m_backend;
        const Bodies* m_bodies;
        Poses* m_poses;

        // The entities with a Vehicle, and with a VehicleWheel, as of the last Drain.
        TrackedComponents m_vehicleEntities;
        TrackedComponents m_wheelEntities;
        std::map<std::uint32_t, VehicleRecord> m_vehicles;
        // Vehicles and wheels that are inactive now: each is warned about once, when it becomes so.
        std::set<std::uint32_t> m_inactiveVehicles;
        std::set<std::uint32_t> m_inactiveWheels;
        // The records are kept for the entities in their tracked sets.
        TrackedComponents m_differentialEntities;
        TrackedComponents m_antiRollBarEntities;
        std::map<std::uint32_t, VehicleLinkRecord> m_differentials;
        std::map<std::uint32_t, VehicleLinkRecord> m_antiRollBars;
        // The kinds of out-of-range value each wheel entity has been warned about, so each is warned about once.
        std::map<std::uint32_t, std::uint32_t> m_wheelWarnings;
    };
} // namespace Fenrir::Detail
