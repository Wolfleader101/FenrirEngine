#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Fenrir
{
    /** @brief How Assets::Move went. */
    enum class MoveStatus : std::uint8_t
    {
        /**
         * @brief The file was moved and every reference to it rewritten; on a dry run, nothing was written, and
         * MoveResult lists what would have been.
         */
        Succeeded,
        /**
         * @brief Nothing was written, and MoveResult::problem says why: a path refused, `from` not a file, or the root
         * not scanned through.
         */
        Refused,
        /**
         * @brief A write failed partway, MoveResult::problem saying which. The file was moved unless the move itself
         * failed, and MoveResult::changed lists each file rewritten before the failure.
         */
        Failed,
    };

    /** @brief A `.json5` file under the root that Assets::Move left as it was, and why. */
    struct UnscannedFile
    {
        /** @brief Its asset path, root-relative with forward slashes. */
        std::string path;
        /**
         * @brief Why it was not scanned: the located syntax error of a file that does not parse, the type a file
         * names that the registry given lacks, or what makes it no envelope.
         */
        std::string reason;
    };

    /** @brief What Assets::Move gives back (ADR-0079 §6): how it went, each file it rewrote and each it could not read.
     */
    struct MoveResult
    {
        /** @brief Whether the file moved. */
        MoveStatus status = MoveStatus::Succeeded;
        /** @brief Why the move was refused or failed; empty when it succeeded. */
        std::string problem;
        /**
         * @brief Each envelope whose references were rewritten, by its asset path after the move (the moved file's
         * own included, by its new path), in path order.
         */
        std::vector<std::string> changed;
        /** @brief How many references were rewritten, across every file in `changed`. */
        std::size_t references = 0;
        /**
         * @brief Each `.json5` file under the root left as it was because it could not be read for references, in path
         * order: any reference to the moved file it holds still names the old path.
         */
        std::vector<UnscannedFile> unscanned;
    };
} // namespace Fenrir
