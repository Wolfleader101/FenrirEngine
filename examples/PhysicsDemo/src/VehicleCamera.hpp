#pragma once

#include <algorithm>
#include <cmath>
#include <numbers>

#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirMath/Math.hpp"
#include "SceneLooks.hpp"

namespace PhysicsDemo
{
    /**
     * @brief The vehicle scenes' chase camera, as game code writes one: behind and above what it follows, turning with
     * it a little late, and writing where it is into its Camera entity's Transform. Switched off, it leaves the camera
     * where it is. Vehicles (C++), Motorcycle (C++) and Tank (C++) share it, as the legacy scenes did.
     */
    class VehicleCamera
    {
      public:
        [[nodiscard]] bool Following() const
        {
            return m_following;
        }

        void Toggle()
        {
            m_following = !m_following;
        }

        /** @brief Degrees about world up the camera looks along, as LookRotation's, with what the stick has turned. */
        [[nodiscard]] float Yaw() const
        {
            return m_yaw + m_orbit.x;
        }

        /** @brief A target facing the way the camera looks, so that following it, the camera keeps its heading. */
        [[nodiscard]] Fenrir::Math::Quat Ahead() const
        {
            return Fenrir::Math::AngleAxis(-(m_yaw + 90.0f) * degrees, Fenrir::Math::Vec3(0.0f, 1.0f, 0.0f));
        }

        /** @brief Turns the camera by `by` degrees about world up, + to the right. */
        void Turn(float by)
        {
            m_yaw += by;
        }

        /**
         * @brief Each frame: swings round behind `target`'s forward (-Z) and writes the camera into `camera`. `look`,
         * each axis in [-1, 1] as a gamepad's stick, turns it round the target and tilts it; at zero it springs back
         * behind.
         */
        void Update(Fenrir::Transform& camera, const Fenrir::Transform& target, const Fenrir::Math::Vec2& look,
                    float dt)
        {
            if (!m_following)
            {
                return;
            }
            Fenrir::Math::Vec3 ahead = target.rot * Fenrir::Math::Vec3(0.0f, 0.0f, -1.0f);
            ahead.y = 0.0f;
            // Facing straight up or down, it keeps the way it looked.
            if (Fenrir::Math::Magnitude(ahead) > 0.1f)
            {
                float turn = std::atan2(ahead.z, ahead.x) / degrees - m_yaw;
                turn -= 360.0f * std::round(turn / 360.0f);
                m_yaw += turn * std::min(1.0f, catchUp * dt);
            }
            // Pushed down, the stick looks down from higher up.
            const Fenrir::Math::Vec2 orbit(look.x * orbitYawDegrees, -look.y * orbitPitchDegrees);
            m_orbit += (orbit - m_orbit) * std::min(1.0f, orbitCatchUp * dt);
            const float yaw = (m_yaw + m_orbit.x) * degrees;
            const float pitch = (pitchDegrees + m_orbit.y) * degrees;
            const Fenrir::Math::Vec3 front(std::cos(pitch) * std::cos(yaw), std::sin(pitch),
                                           std::cos(pitch) * std::sin(yaw));
            camera.pos = target.pos + Fenrir::Math::Vec3(0.0f, height, 0.0f) - front * distance;
            camera.rot = LookRotation(m_yaw + m_orbit.x, pitchDegrees + m_orbit.y);
        }

      private:
        static constexpr float degrees = std::numbers::pi_v<float> / 180.0f;
        static constexpr float distance = 9.0f;
        static constexpr float height = 1.5f;
        static constexpr float pitchDegrees = -15.0f;
        static constexpr float catchUp = 3.0f;           // How much of its turn it makes up per second.
        static constexpr float orbitYawDegrees = 180.0f; // A full push sideways looks at the target from the front.
        static constexpr float orbitPitchDegrees = 20.0f;
        static constexpr float orbitCatchUp = 6.0f; // As catchUp, for following the stick and springing back.

        float m_yaw = -90.0f;
        Fenrir::Math::Vec2 m_orbit = Fenrir::Math::Vec2(0.0f); // Degrees of yaw and pitch the stick has turned it by.
        bool m_following = true;
    };
} // namespace PhysicsDemo
