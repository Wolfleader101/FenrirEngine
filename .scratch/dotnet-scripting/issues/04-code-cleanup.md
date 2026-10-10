# 04: Code cleanup from the review

**What to build:** Apply the review's cleanup findings so later API work builds on a lean, consistent base. Replace the repeated status/value/diagnostic/truncated result structs with one shared result shape. Cut the README down to a usage guide. Strip over-hedged and repetitive comments, leaving the essential lifetime and ownership contracts. Fix the correctness findings assigned to this ticket.

**Blocked by:** 01

**Status:** ready-for-agent

- [ ] Every finding the review assigned to this ticket is fixed or explicitly deferred with a reason.
- [ ] One shared result type (or template) replaces the duplicated result structs, and call sites read consistently.
- [ ] The README is a concise usage guide: build, configure, run, and core lifetime rules. Design rationale moves to ADRs or the plan.
- [ ] Comments state contracts once, at the declaration, and are not repeated at every call site.
- [ ] There are no behaviour changes beyond the listed bug fixes. All CTests pass, and the Release unload checks still pass.

## Comments

From the ticket 03 review (review 01, O5): the fixture bridge still selects behaviour by magic numbers. `left` values 1–12 pick integer responses, 100–103 read counters, and 200+ select string or unload modes. Replace them with named scenarios as part of this cleanup.
