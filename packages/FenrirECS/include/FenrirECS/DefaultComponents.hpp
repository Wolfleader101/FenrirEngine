#pragma once

#include "FenrirMath/Math.hpp"

#include "Entity.hpp"

namespace Fenrir
{
    struct Transform
    {
        Transform() = default;

        Transform(const Math::Vec3& pos, const Math::Quat& rot, const Math::Vec3& scale)
            : pos(pos), rot(rot), scale(scale)
        {
        }

        Math::Vec3 pos = Math::Vec3(0.0f, 0.0f, 0.0f);
        Math::Quat rot = Math::Quat(1.0f, 0.0f, 0.0f, 0.0f);
        Math::Vec3 scale = Math::Vec3(1.0f, 1.0f, 1.0f);
    };

    inline Math::Mat4 TransformToMat4(const Transform& transform)
    {
        Math::Mat4 mdl_mat = Math::Mat4(1.0f);

        mdl_mat = Math::Translate(mdl_mat, transform.pos);

        mdl_mat *= Math::Mat4Cast(transform.rot);

        mdl_mat = Math::Scale(mdl_mat, transform.scale);

        return mdl_mat;
    }

    struct Name
    {

        Name() = default;

        /**
         * @brief Construct a new Name object
         *
         * @param newName The name to set
         */
        void Set(const char* newName)
        {
            Clear();
            std::strncpy(name, newName, sizeof(name) - 1); // -1 to leave space for the null terminator
            name[sizeof(name) - 1] = '\0';
        }

        /**
         * @brief Get the name
         *
         * @return const char*
         */
        const char* Get() const
        {
            return name;
        }

        /**
         * @brief Clear the name
         *
         */
        void Clear()
        {
            std::memset(name, 0, sizeof(name));
        }

        /**
         * @brief Check if the name is empty
         *
         * @return true if the name is empty
         * @return false if the name is not empty
         */
        bool IsEmpty() const
        {
            return name[0] == '\0';
        }

        /**
         * @brief Get the length of the name
         *
         * @return size_t The length of the name
         */
        size_t Length() const
        {
            return std::strlen(name);
        }

      private:
        // max name length is 255 characters
        char name[256] = {0};
    };

    struct Relationship
    {
        Relationship() = default;

        Entity parent = Entity();
        Entity firstChild = Entity();
        Entity nextSibling = Entity();
        Entity prevSibling = Entity();
    };

} // namespace Fenrir