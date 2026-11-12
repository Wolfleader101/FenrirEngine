#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "FenrirUI/Style.hpp"
#include "FenrirUI/Stylesheet.hpp"
#include "StyleAnimation.hpp"
#include "StyleValues.hpp"

namespace Fenrir::Detail
{
    struct UIRootState;

    /** @brief An element's custom properties, its own over those it inherits; shared down the tree while unchanged. */
    using CustomProperties = std::unordered_map<std::string, std::string>;

    /**
     * @brief A run of a rule's declarations: those read when the sheet was added, then at most one that waits for its
     * `var()`s, so a rule's declarations apply in the order written.
     */
    struct RuleSegment
    {
        StyleDeclarations values;
        std::optional<StyleDeclaration> deferred;
    };

    /** @brief A rule read for the cascade. */
    struct CompiledRule
    {
        std::vector<RuleSegment> segments;
        /** @brief Its custom properties, in the order written. */
        std::vector<StyleDeclaration> customProperties;
        /** @brief Its place in the cascade's order: later sheets, then later rules, are higher. */
        std::uint32_t order = 0;
    };

    /** @brief A sheet a root holds: the sheet as parsed, its rules read for the cascade and its keyframes read. */
    struct StylesheetEntry
    {
        StylesheetId id;
        Stylesheet sheet;
        std::vector<CompiledRule> rules;
        std::vector<std::shared_ptr<const CompiledKeyframes>> keyframes;
    };

    /** @brief One selector of a rule, with the rule it belongs to. */
    struct IndexedSelector
    {
        const Selector* selector = nullptr;
        const CompiledRule* rule = nullptr;
    };

    /**
     * @brief Every selector of a root's sheets, bucketed by its subject's id, else its first class, else its tag, so an
     * element is tested against the selectors that could match it alone.
     */
    struct StyleIndex
    {
        std::vector<IndexedSelector> selectors;
        std::unordered_map<std::string, std::vector<std::uint32_t>> byId;
        std::unordered_map<std::string, std::vector<std::uint32_t>> byClass;
        std::unordered_map<std::string, std::vector<std::uint32_t>> byTag;
        std::vector<std::uint32_t> any;
        /** @brief Some selector looks above its subject, so a change to an element can restyle its descendants. */
        bool hasCombinators = false;
        /** @brief Some selector uses :first-child or :last-child, so a change of children can restyle siblings. */
        bool hasSiblingStructure = false;
        /** @brief Some selector uses :intro, so clearing it can restyle. */
        bool hasIntro = false;
        /** @brief Every sheet's `@keyframes` by name. */
        KeyframesByName keyframes;
    };

    /**
     * @brief Reads a parsed sheet's rules for the cascade and its keyframes for animations, loading its url()s; what
     * fails to read is logged.
     */
    void CompileStylesheet(UIRootState& state, StylesheetEntry& entry);

    /** @brief Builds the index, and the keyframes by name, over every sheet the root holds, in the order added. */
    void RebuildStyleIndex(UIRootState& state);

    /**
     * @brief Resolves the cascaded style of the element in slot `index` onto `style`, which holds the defaults and
     * what it inherits: its matching rules by specificity and order, then its inline style. `custom` is its custom
     * properties, what it inherits on the way in.
     */
    void Cascade(UIRootState& state, std::uint32_t index, ResolvedStyle& style,
                 std::shared_ptr<const CustomProperties>& custom);

    /** @brief The context that loads a url() from the root's asset store. */
    [[nodiscard]] StyleParseContext MakeParseContext(UIRootState& state);
} // namespace Fenrir::Detail
