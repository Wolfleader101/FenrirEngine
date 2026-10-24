# Coding standards

The rules a reviewer applies. Anything a tool can check is checked by the tool (`.clang-format`, the `.githooks/pre-commit` hook and the CI formatting job, the `ScriptingDoesNotUseEnTT` CTest); this file holds the judgement calls.

## Keep the documents true

- A change that makes a statement in an ADR, `GLOSSARY.md` or a README false amends that statement in the same change. A reader who finds an ADR contradicting the code stops trusting all of them.
- Use the `GLOSSARY.md` words in code, comments, log messages and tests. Where the glossary has a term for a concept, a second name for it is a defect.

## Design

- Prefer deep modules: a small interface over a lot of behaviour, with the seam where something varies. Vocabulary and the deletion test are in the `codebase-design` skill; reviewers also apply its smell baseline (Duplicated Code, Data Clumps, Feature Envy and the rest) as judgement calls.
- Code outside `packages/FenrirECS` does not name EnTT types (ADR-0008).
- Scripting follows the ECS-first model of ADR-0007: components are data, systems are logic.

## Comments

- A declaration someone else calls carries a Doxygen block, `/** @brief ... */`, directly above it. Nothing sits between the block and what it documents: forward declarations go above the block.
- Inside a function, a `//` comment says why, not what.
- A comment that describes behaviour is part of the change that alters the behaviour.

## Tests

- A test crosses a seam that was agreed in the spec, almost always a module's public interface, and asserts what a caller can observe. It does not reach into private state; a seam the tests need, such as `ScriptingProbe`, is named and kept out of the product interface.
- One behaviour per `TEST_CASE`, named for the behaviour. Each check is a `REQUIRE` with an `INFO` message that says what went wrong in terms of the behaviour.
- The expected value comes from somewhere other than the code under test: a literal, a worked example, the spec.
- A test that must run in declaration order says so where it is declared and is run with `--order decl`.

## Changes

- Commit messages say why; the diff says what.
- A refactor leaves behaviour, messages and ordering unchanged, and the tests that pin them pass before and after.
