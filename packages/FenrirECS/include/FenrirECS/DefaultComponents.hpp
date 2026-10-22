#pragma once

#include "FenrirMath/Math.hpp"

#include <cstring>

namespace Fenrir
{
    /**
     * @brief An entity's place in the world, relative to its parent: every entity has one.
     */
    struct Transform
    {
        Transform() = default;

        Transform(const Math::Vec3& pos, const Math::Quat& rot, const Math::Vec3& scale)
            : pos(pos), rot(rot), scale(scale)
        {
        }

        /** @brief The position, relative to the parent. */
        Math::Vec3 pos = Math::Vec3(0.0f, 0.0f, 0.0f);
        /** @brief The rotation, relative to the parent. */
        Math::Quat rot = Math::Quat(1.0f, 0.0f, 0.0f, 0.0f);
        /** @brief The scale on each axis, relative to the parent. */
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

    /**
     * @brief An entity's name: a fixed 256-byte UTF-8 buffer, so at most 255 bytes of text. Every entity has one.
     */
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
        /** @brief The bytes, zero-terminated (so the name is at most 255 bytes). */
        char name[256] = {0};
    };

} // namespace Fenrir