#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Fenrir
{
    /** @brief Something a stylesheet's parser refused or did not know, and the line it was on (from 1). */
    struct StyleDiagnostic
    {
        std::uint32_t line = 0;
        std::string message;
    };

    /**
     * @brief One declaration of a rule, keyframe or font face: a property's name (lower case; a custom property's
     * `--name` as written) and its value as written, trimmed. Shorthands are expanded into their longhands when the
     * sheet is parsed, unless the value holds a `var()`, which waits for the cascade.
     */
    struct StyleDeclaration
    {
        std::string name;
        std::string value;
        std::uint32_t line = 0;

        friend bool operator==(const StyleDeclaration&, const StyleDeclaration&) = default;
    };

    /** @brief How a compound selector is joined to the one before it. */
    enum class SelectorCombinator : std::uint8_t
    {
        /** @brief Whitespace: any ancestor. */
        Descendant,
        /** @brief `>`: the parent. */
        Child,
    };

    /** @brief A structural pseudo-class: one an element has by where it is, not a flag code sets. */
    enum class StructuralPseudoClass : std::uint8_t
    {
        FirstChild = 1u << 0u,
        LastChild = 1u << 1u,
        Root = 1u << 2u,
    };

    /** @brief A compound selector: a tag (empty for any), an id, classes and pseudo-classes, all of which must hold. */
    struct CompoundSelector
    {
        std::string tag;
        std::string id;
        std::vector<std::string> classes;
        /** @brief PseudoClass bits, all of which the element must carry. */
        std::uint16_t pseudoClasses = 0;
        /** @brief StructuralPseudoClass bits. */
        std::uint8_t structural = 0;
    };

    /**
     * @brief A complex selector: compound selectors from the leftmost to the subject, the rightmost, each after the
     * first joined to the one before it by `combinators[i - 1]`.
     */
    struct Selector
    {
        std::vector<CompoundSelector> compounds;
        std::vector<SelectorCombinator> combinators;
        /** @brief CSS's specificity: ids, then classes and pseudo-classes, then tags, each in 10 bits. */
        std::uint32_t specificity = 0;

        /** @brief As CSS writes it, in one canonical form (`panel.a:hover > #b`). */
        [[nodiscard]] std::string ToString() const;
    };

    /** @brief A style rule, nesting flattened: its selector list and its declarations, in the order written. */
    struct StyleRule
    {
        std::vector<Selector> selectors;
        std::vector<StyleDeclaration> declarations;
        std::uint32_t line = 0;
    };

    /** @brief One block of a `@keyframes` rule: where it sits (0 to 1, one per selector) and what it declares. */
    struct StyleKeyframe
    {
        std::vector<float> offsets;
        std::vector<StyleDeclaration> declarations;
    };

    /** @brief A `@keyframes` rule, kept for animations. */
    struct StyleKeyframes
    {
        std::string name;
        std::vector<StyleKeyframe> keyframes;
        std::uint32_t line = 0;
    };

    /** @brief A `@font-face` rule's descriptors as written (`font-family`, `src`, ...), kept for fonts. */
    struct StyleFontFace
    {
        std::vector<StyleDeclaration> descriptors;
        std::uint32_t line = 0;
    };

    /** @brief An `@import` of another sheet, recorded where it was written. */
    struct StyleImport
    {
        std::string url;
        std::uint32_t line = 0;
    };

    /** @brief Names one stylesheet a UI root holds; zero names none. */
    struct StylesheetId
    {
        std::uint32_t value = 0;

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return value != 0;
        }

        friend constexpr bool operator==(StylesheetId, StylesheetId) noexcept = default;
    };

    /**
     * @brief A parsed stylesheet (ADR-0064): CSS rules with nesting flattened, `@keyframes`, `@font-face` and
     * `@import`, and what the parser refused. Parse never fails as a whole: a malformed declaration, an unknown
     * property or a declaration whose value cannot be read is skipped; a malformed rule or one with an unknown
     * pseudo-class is skipped whole; each is a diagnostic with its line, and the rest of the sheet stands.
     */
    class Stylesheet
    {
      public:
        /** @brief Parses `text`; `name` names it in what the root logs. */
        [[nodiscard]] static Stylesheet Parse(std::string_view text, std::string_view name);

        std::string name;
        /** @brief In the order written, a nested rule after the rule it is in. */
        std::vector<StyleRule> rules;
        std::vector<StyleKeyframes> keyframes;
        std::vector<StyleFontFace> fontFaces;
        std::vector<StyleImport> imports;
        std::vector<StyleDiagnostic> diagnostics;
    };
} // namespace Fenrir
