#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include "AssetSource.hpp"

namespace AssetsTest
{
    // An asset source over another, counting its reads in `reads`, so a test can tell how often the store read a file
    // through its source, or that it read none.
    class CountingSource : public Fenrir::Detail::AssetSource
    {
      public:
        CountingSource(std::unique_ptr<Fenrir::Detail::AssetSource> inner, std::shared_ptr<std::size_t> reads)
            : m_inner(std::move(inner)), m_reads(std::move(reads))
        {
        }

        bool Exists(std::string_view path) override
        {
            return m_inner->Exists(path);
        }

        std::optional<std::vector<std::byte>> Read(std::string_view path) override
        {
            ++*m_reads;
            return m_inner->Read(path);
        }

      private:
        std::unique_ptr<Fenrir::Detail::AssetSource> m_inner;
        std::shared_ptr<std::size_t> m_reads;
    };
} // namespace AssetsTest
