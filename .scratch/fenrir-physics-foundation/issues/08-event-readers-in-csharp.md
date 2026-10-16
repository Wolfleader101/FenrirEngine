# 08: Event readers in C#, and scripting migrates

**What to build:** A C# system reads each event once through an `EventReader<T>` field (`foreach (var e in reader.Read())`), with the same guarantee as C++ (ADR-0012; spec: "FenrirScripting"). Sends still apply when the C# system returns. Scripting's own reading (its bindings, and the scripting demo's `LogBursts`, whose hand-rolled count goes away) moves onto readers. Migration batch for scripting.

**Blocked by:** 07 (Event readers in C++).

**Status:** resolved

- [ ] C# systems declare `EventReader<T>` fields and read each event once; C# and C++ readers of one queue are independent.
- [ ] `EventsHaveTheSameFrameWindowInBothLanguages` is replaced by a test that each reader sees each event once in both languages, across `Update` and `Tick` readers and late sends.
- [ ] Scripting's bindings no longer call `ReadEvents`.
- [ ] `LogBursts` reads through a reader and no longer counts by hand; the DemoApp's scripting demo behaves as before.
- [ ] All scripting tests pass.

**Resolved by:** merge of impl/08-csharp-event-readers (7301cd7)

**Hand checked (2026-10-16):** the DemoApp scripting demo (LogBursts, Controls, Sparks) handles each event once (AutoPlay run logged each once during the build).
