#pragma once

// Vehicles' settings (ADR-0015): how the diff compares a vehicle's components with what physics last applied, how their
// out-of-range values are corrected, each kind of problem warned about, and the backend descriptions built from them.
// Part of the Vehicles module, read by Vehicles.cpp alone. Private to the FenrirPhysics package.

#include <cstdint>
#include <vector>

#include "Backend/Backend.hpp"
#include "FenrirMath/Math.hpp"
#include "FenrirPhysics/Vehicles.hpp"

namespace Fenrir
{
    class ILogger;
} // namespace Fenrir

namespace Fenrir::Detail
{
    /** @brief Whether two wheels have the same settings: every field but the vehicle it names and its state. */
    [[nodiscard]] bool Same(const VehicleWheel& a, const VehicleWheel& b);

    /** @brief Whether two driver inputs are the same, bit for bit. */
    [[nodiscard]] bool Same(const VehicleInput& a, const VehicleInput& b);

    /** @brief Whether two kinds are the same kind of vehicle with the same settings. */
    [[nodiscard]] bool Same(const VehicleKind& a, const VehicleKind& b);

    /** @brief Whether two vehicles' differentials, as given to the backend, are the same. */
    [[nodiscard]] bool Same(const std::vector<Backend::VehicleDifferentialDesc>& a,
                            const std::vector<Backend::VehicleDifferentialDesc>& b);

    /** @brief Whether two vehicles' anti-roll bars, as given to the backend, are the same. */
    [[nodiscard]] bool Same(const std::vector<Backend::VehicleAntiRollBarDesc>& a,
                            const std::vector<Backend::VehicleAntiRollBarDesc>& b);

    /** @brief Driver input in its ranges for how the vehicle is driven (`drive`), silently, as joint position targets
     * are. In Manual `forward` is a pedal, so reversing is the gear's job. */
    [[nodiscard]] VehicleInput Clamped(const VehicleInput& input, const Backend::VehicleDriveDesc& drive);

    /** @brief A tilt limit from 0 to a half turn, silently: a half turn or more is off. */
    [[nodiscard]] float TiltOf(const Vehicle& vehicle);

    /**
     * @brief How the Vehicle on `entity` is driven, from its kind corrected into the ranges the backend can use. The
     * kinds of out-of-range value it held that `warned` does not have are warned about through `logger`, and `warned`
     * becomes those it holds now, so a valid value clears its warning. A trailer is an unpowered wheeled vehicle with
     * the default engine and a Manual gearbox, held in neutral by Clamped.
     */
    [[nodiscard]] Backend::VehicleDriveDesc DriveDescOf(const VehicleKind& kind, std::uint32_t entity,
                                                        std::uint32_t& warned, ILogger& logger);

    /** @brief The backend's wheel for the VehicleWheel on `entity`, its settings corrected (each kind of out-of-range
     * value warned about once per wheel, `warned` kept with it) and its position scaled by `scale`, the chassis's world
     * scale. */
    [[nodiscard]] Backend::VehicleWheelDesc WheelDescOf(std::uint32_t entity, const VehicleWheel& wheel,
                                                        const Math::Vec3& scale, std::uint32_t& warned,
                                                        ILogger& logger);

    /** @brief The backend's differential for the VehicleDifferential on `entity`, between the wheels at indices `left`
     * and `right` (-1 for none), its out-of-range values corrected, each kind warned about once per component. */
    [[nodiscard]] Backend::VehicleDifferentialDesc DifferentialDescOf(std::uint32_t entity,
                                                                      const VehicleDifferential& differential, int left,
                                                                      int right, std::uint32_t& warned,
                                                                      ILogger& logger);

    /** @brief The backend's anti-roll bar for the VehicleAntiRollBar on `entity`, between the wheels at indices `left`
     * and `right`, its out-of-range values corrected, each kind warned about once per component. */
    [[nodiscard]] Backend::VehicleAntiRollBarDesc AntiRollBarDescOf(std::uint32_t entity, const VehicleAntiRollBar& bar,
                                                                    int left, int right, std::uint32_t& warned,
                                                                    ILogger& logger);
} // namespace Fenrir::Detail
