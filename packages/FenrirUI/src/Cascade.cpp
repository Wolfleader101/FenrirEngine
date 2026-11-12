#include "Cascade.hpp"

#include <algorithm>
#include <utility>

#include "FenrirApp/App.hpp"
#include "FenrirAssets/AssetStore.hpp"
#include "UIRootState.hpp"

namespace Fenrir::Detail
{
    namespace
    {
        bool MatchCompound(const UIRootState& state, ElementId id, const CompoundSelector& compound)
        {
            const ElementSlot& slot = *state.Find(id);
            if (!compound.tag.empty() && compound.tag != slot.tag)
            {
                return false;
            }
            if (!compound.id.empty() && compound.id != slot.idString)
            {
                return false;
            }
            if ((slot.pseudoClasses & compound.pseudoClasses) != compound.pseudoClasses)
            {
                return false;
            }
            for (const std::string& name : compound.classes)
            {
                if (std::find(slot.classes.begin(), slot.classes.end(), name) == slot.classes.end())
                {
                    return false;
                }
            }
            if (compound.structural != 0)
            {
                const auto has = [&](StructuralPseudoClass flag) {
                    return (compound.structural & static_cast<std::uint8_t>(flag)) != 0;
                };
                if (has(StructuralPseudoClass::Root) && id != state.rootElement)
                {
                    return false;
                }
                const ElementSlot* parent = state.Find(slot.parent);
                if (has(StructuralPseudoClass::FirstChild) && (parent == nullptr || parent->children.front() != id))
                {
                    return false;
                }
                if (has(StructuralPseudoClass::LastChild) && (parent == nullptr || parent->children.back() != id))
                {
                    return false;
                }
            }
            return true;
        }

        /** @brief Whether compounds up to `last` match with `last` on `id`, trying each ancestor for a descendant. */
        bool MatchFrom(const UIRootState& state, const Selector& selector, std::size_t last, ElementId id)
        {
            if (!MatchCompound(state, id, selector.compounds[last]))
            {
                return false;
            }
            if (last == 0)
            {
                return true;
            }
            ElementId above = state.Find(id)->parent;
            if (selector.combinators[last - 1] == SelectorCombinator::Child)
            {
                return above.IsValid() && MatchFrom(state, selector, last - 1, above);
            }
            for (; above.IsValid(); above = state.Find(above)->parent)
            {
                if (MatchFrom(state, selector, last - 1, above))
                {
                    return true;
                }
            }
            return false;
        }

        void Append(std::vector<std::uint32_t>& out, const std::vector<std::uint32_t>* bucket)
        {
            if (bucket != nullptr)
            {
                out.insert(out.end(), bucket->begin(), bucket->end());
            }
        }

        const std::vector<std::uint32_t>* Bucket(const std::unordered_map<std::string, std::vector<std::uint32_t>>& map,
                                                 const std::string& key)
        {
            const auto found = map.find(key);
            return found != map.end() ? &found->second : nullptr;
        }
    } // namespace

    StyleParseContext MakeParseContext(UIRootState& state)
    {
        StyleParseContext context;
        App& app = state.app;
        context.loadImage = [&app](std::string_view path) -> AssetHandle<Image> {
            AssetStore* store = app.TryGetResource<AssetStore>();
            if (store == nullptr)
            {
                return {};
            }
            return store->Load<Image>(path, AssetOwner::App()).handle;
        };
        return context;
    }

    void CompileStylesheet(UIRootState& state, StylesheetEntry& entry)
    {
        StyleParseContext context = MakeParseContext(state);
        entry.rules.clear();
        for (const StyleRule& rule : entry.sheet.rules)
        {
            CompiledRule& compiled = entry.rules.emplace_back();
            compiled.segments.emplace_back();
            for (const StyleDeclaration& declaration : rule.declarations)
            {
                if (declaration.name.starts_with("--"))
                {
                    compiled.customProperties.push_back(declaration);
                    continue;
                }
                if (ContainsVar(declaration.value))
                {
                    compiled.segments.back().deferred = declaration;
                    compiled.segments.emplace_back();
                    continue;
                }
                context.error.clear();
                if (!ParseStyleDeclaration(declaration.name, declaration.value, compiled.segments.back().values,
                                           context))
                {
                    state.app.Logger()->Warn("FenrirUI: {}:{}: the declaration '{}: {}' was skipped: {}",
                                             entry.sheet.name, declaration.line, declaration.name, declaration.value,
                                             context.error);
                }
            }
        }
    }

    void RebuildStyleIndex(UIRootState& state)
    {
        StyleIndex index;
        std::uint32_t order = 0;
        for (const std::unique_ptr<StylesheetEntry>& entry : state.sheets)
        {
            for (std::size_t r = 0; r < entry->rules.size(); ++r)
            {
                CompiledRule& rule = entry->rules[r];
                rule.order = order++;
                for (const Selector& selector : entry->sheet.rules[r].selectors)
                {
                    const auto at = static_cast<std::uint32_t>(index.selectors.size());
                    index.selectors.push_back({&selector, &rule});
                    index.hasCombinators = index.hasCombinators || selector.compounds.size() > 1;
                    for (const CompoundSelector& compound : selector.compounds)
                    {
                        constexpr auto siblings = static_cast<std::uint8_t>(StructuralPseudoClass::FirstChild) |
                                                  static_cast<std::uint8_t>(StructuralPseudoClass::LastChild);
                        index.hasSiblingStructure = index.hasSiblingStructure || (compound.structural & siblings) != 0;
                    }
                    const CompoundSelector& subject = selector.compounds.back();
                    if (!subject.id.empty())
                    {
                        index.byId[subject.id].push_back(at);
                    }
                    else if (!subject.classes.empty())
                    {
                        index.byClass[subject.classes.front()].push_back(at);
                    }
                    else if (!subject.tag.empty())
                    {
                        index.byTag[subject.tag].push_back(at);
                    }
                    else
                    {
                        index.any.push_back(at);
                    }
                }
            }
        }
        state.index = std::move(index);
    }

    void Cascade(UIRootState& state, std::uint32_t index, ResolvedStyle& style,
                 std::shared_ptr<const CustomProperties>& custom)
    {
        ElementSlot& slot = state.slots[index];
        const ElementId id{index, slot.version};
        const StyleIndex& styles = state.index;
        if (!styles.selectors.empty())
        {
            std::vector<std::uint32_t>& candidates = state.candidates;
            candidates.clear();
            if (!slot.idString.empty())
            {
                Append(candidates, Bucket(styles.byId, slot.idString));
            }
            for (const std::string& name : slot.classes)
            {
                Append(candidates, Bucket(styles.byClass, name));
            }
            Append(candidates, Bucket(styles.byTag, slot.tag));
            Append(candidates, &styles.any);
            std::sort(candidates.begin(), candidates.end());
            candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());

            std::vector<IndexedSelector>& matched = state.matched;
            matched.clear();
            for (const std::uint32_t at : candidates)
            {
                const IndexedSelector& candidate = styles.selectors[at];
                if (MatchFrom(state, *candidate.selector, candidate.selector->compounds.size() - 1, id))
                {
                    matched.push_back(candidate);
                }
            }
            // A rule with several matching selectors counts once, at its highest specificity.
            std::sort(matched.begin(), matched.end(), [](const IndexedSelector& a, const IndexedSelector& b) {
                return a.rule != b.rule ? a.rule->order < b.rule->order
                                        : a.selector->specificity > b.selector->specificity;
            });
            matched.erase(
                std::unique(matched.begin(), matched.end(),
                            [](const IndexedSelector& a, const IndexedSelector& b) { return a.rule == b.rule; }),
                matched.end());
            std::stable_sort(matched.begin(), matched.end(), [](const IndexedSelector& a, const IndexedSelector& b) {
                return a.selector->specificity != b.selector->specificity
                           ? a.selector->specificity < b.selector->specificity
                           : a.rule->order < b.rule->order;
            });

            const bool declaresCustom = std::any_of(matched.begin(), matched.end(), [](const IndexedSelector& m) {
                return !m.rule->customProperties.empty();
            });
            if (declaresCustom)
            {
                auto own = std::make_shared<CustomProperties>(custom != nullptr ? *custom : CustomProperties{});
                std::vector<std::string> declared;
                for (const IndexedSelector& match : matched)
                {
                    for (const StyleDeclaration& declaration : match.rule->customProperties)
                    {
                        (*own)[declaration.name] = declaration.value;
                        declared.push_back(declaration.name);
                    }
                }
                // As CSS, a custom property's var()s resolve on the element that declares it, and its descendants
                // inherit the result; one that cannot resolve (a missing name, a cycle) is as if not declared.
                const CustomProperties raw = *own;
                const auto rawLookup = [&raw](std::string_view name) -> const std::string* {
                    const auto found = raw.find(std::string(name));
                    return found != raw.end() ? &found->second : nullptr;
                };
                for (const std::string& name : declared)
                {
                    const std::string& value = raw.at(name);
                    if (!ContainsVar(value))
                    {
                        continue;
                    }
                    if (std::optional<std::string> resolved = SubstituteVars(value, rawLookup))
                    {
                        (*own)[name] = std::move(*resolved);
                    }
                    else
                    {
                        own->erase(name);
                    }
                }
                custom = std::move(own);
            }

            const auto lookup = [&custom](std::string_view name) -> const std::string* {
                if (custom == nullptr)
                {
                    return nullptr;
                }
                const auto found = custom->find(std::string(name));
                return found != custom->end() ? &found->second : nullptr;
            };
            // What the element has with no rule: the defaults and what it inherits.
            std::optional<ResolvedStyle> undeclared;
            if (std::any_of(matched.begin(), matched.end(),
                            [](const IndexedSelector& m) { return m.rule->segments.size() > 1; }))
            {
                undeclared = style;
            }
            std::optional<StyleParseContext> context;
            for (const IndexedSelector& match : matched)
            {
                for (const RuleSegment& segment : match.rule->segments)
                {
                    segment.values.ApplyTo(style);
                    if (!segment.deferred)
                    {
                        continue;
                    }
                    if (!context)
                    {
                        context = MakeParseContext(state);
                    }
                    StyleDeclarations read;
                    const std::optional<std::string> value = SubstituteVars(segment.deferred->value, lookup);
                    if (!value || !ParseStyleDeclaration(segment.deferred->name, *value, read, *context))
                    {
                        // As CSS: a var() that leaves nothing readable makes the property as if no rule declared it
                        // (invalid at computed-value time), not the value an earlier rule gave.
                        read.Clear();
                        for (const StyleProperty property : PropertiesOf(segment.deferred->name))
                        {
                            read.CopyFrom(property, *undeclared);
                        }
                    }
                    read.ApplyTo(style);
                }
            }
        }
        state.slots[index].inlineStyle.ApplyTo(style);
    }
} // namespace Fenrir::Detail
