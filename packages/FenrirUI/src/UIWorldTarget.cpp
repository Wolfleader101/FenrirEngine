#include "FenrirUI/UIWorldTarget.hpp"

#include <cmath>
#include <utility>

#include "FenrirApp/App.hpp"
#include "FenrirECS/WorldTransform.hpp"
#include "FenrirRenderer/WorldTargets2D.hpp"
#include "FenrirUI/UIRoot.hpp"

namespace Fenrir
{
    namespace
    {
        /** @brief The entity's quad, when it is valid and has one the renderer would draw. */
        const WorldQuad2D* QuadOf(const Entity& entity)
        {
            if (!entity.IsValid() || !entity.HasComponent<WorldQuad2D>())
            {
                return nullptr;
            }
            const WorldQuad2D& quad = entity.GetComponent<WorldQuad2D>();
            return quad.IsDrawable() ? &quad : nullptr;
        }
    } // namespace

    UIWorldTarget::UIWorldTarget(App& app, Entity entity) : m_entity(entity)
    {
        if (QuadOf(m_entity) == nullptr)
        {
            app.Logger()->Warn("FenrirUI: a world target's entity {} has no WorldQuad2D yet, so its root draws nothing "
                               "until it has one",
                               m_entity.GetId());
        }
    }

    ScaleRule UIWorldTarget::GetDefaultScaleRule() const noexcept
    {
        return ScaleRule::ContentScale;
    }

    UITargetSize UIWorldTarget::GetSize(App&)
    {
        const WorldQuad2D* quad = QuadOf(m_entity);
        if (quad == nullptr)
        {
            return {};
        }
        return {quad->LogicalSize(), 1.0f};
    }

    void UIWorldTarget::Submit(App& app, DrawList2D list)
    {
        if (WorldTargets2D* targets = app.TryGetResource<WorldTargets2D>(); targets != nullptr)
        {
            targets->Submit(m_entity, std::move(list));
        }
    }

    std::optional<Math::Vec2> UIWorldTarget::PointerAt(App&, const Math::Ray& ray)
    {
        const WorldQuad2D* quad = QuadOf(m_entity);
        if (quad == nullptr)
        {
            return std::nullopt;
        }
        // The ray in the entity's space, where the quad is its XY plane, centred on its origin.
        const Math::Mat4 toLocal = Math::Inverse(GetWorldMatrix(m_entity));
        const Math::Vec3 origin(toLocal * Math::Vec4(ray.origin, 1.0f));
        const Math::Vec3 direction(toLocal * Math::Vec4(ray.dir, 0.0f));
        constexpr float along = 1e-6f;
        if (std::abs(direction.z) < along)
        {
            return std::nullopt;
        }
        const float distance = -origin.z / direction.z;
        if (distance < 0.0f)
        {
            return std::nullopt;
        }
        return quad->EntityToLocal(origin + direction * distance);
    }
} // namespace Fenrir
