#pragma once

#include "Device/GraphicsSurface.hpp"

namespace RendererTest
{
    /**
     * @brief A GraphicsSurface with no window behind it: its pixel size is whatever the test sets, and it counts how
     * often the size is read. The core's tests run on it with the null backend and no App (the spec's seam 1).
     */
    class FakeSurface final : public Fenrir::Graphics::GraphicsSurface
    {
      public:
        Fenrir::Graphics::PixelSize size{64, 32};
        mutable int sizeReads = 0;

        [[nodiscard]] Fenrir::Graphics::PixelSize GetPixelSize() const override
        {
            ++sizeReads;
            return size;
        }

        [[nodiscard]] void* GetProcAddress(const char*) override
        {
            return nullptr;
        }

        bool MakeCurrent() override
        {
            return true;
        }

        void Present() override
        {
        }

        bool SetSwapInterval(int) override
        {
            return true;
        }
    };
} // namespace RendererTest
