#!/usr/bin/env python3
# Checks the headers of the local tickets in .scratch/*/issues/ (docs/agents/issue-tracker.md, docs/agents/triage-labels.md):
# one Status line holding one known word, and one Blocked by line whose ticket numbers name tickets in the same folder.
# Frontier and blocking checks match these lines exactly, so a malformed one silently hides or unblocks a ticket.
# Run from anywhere: python scripts/check_tickets.py. Exits 1 and lists every problem when any ticket fails.

import re
import sys
from pathlib import Path

STATUSES = {"needs-triage", "needs-info", "ready-for-agent", "ready-for-human", "wontfix", "open", "claimed", "resolved"}
NAME = re.compile(r"^\d{2}-[a-z0-9-]+\.md$")
STATUS = re.compile(r"^(?:\*\*)?Status:(?:\*\*)?\s*(.*?)\s*$")
BLOCKED_BY = re.compile(r"^(?:\*\*)?Blocked by:(?:\*\*)?\s*(.*?)\s*$")
TICKET_NUMBER = re.compile(r"\b\d{2}\b")


def check(issues: Path, root: Path) -> list[str]:
    problems = []
    tickets = sorted(issues.glob("*.md"))
    numbers = {t.name[:2] for t in tickets}
    for ticket in tickets:
        where = ticket.relative_to(root).as_posix()
        if not NAME.match(ticket.name):
            problems.append(f"{where}: name is not NN-<slug>.md")
        lines = ticket.read_text(encoding="utf-8").splitlines()
        statuses = [m.group(1) for m in map(STATUS.match, lines) if m]
        blockers = [m.group(1) for m in map(BLOCKED_BY.match, lines) if m]
        if len(statuses) != 1:
            problems.append(f"{where}: has {len(statuses)} Status lines, needs one")
        elif statuses[0] not in STATUSES:
            problems.append(f"{where}: Status '{statuses[0]}' is not one of {', '.join(sorted(STATUSES))}")
        if len(blockers) != 1:
            problems.append(f"{where}: has {len(blockers)} Blocked by lines, needs one")
        else:
            for number in TICKET_NUMBER.findall(blockers[0]):
                if number not in numbers:
                    problems.append(f"{where}: Blocked by {number}, but {issues.relative_to(root).as_posix()} has no ticket {number}")
    return problems


def main() -> int:
    root = Path(__file__).resolve().parent.parent
    problems = []
    for issues in sorted((root / ".scratch").glob("*/issues")):
        problems += check(issues, root)
    for problem in problems:
        print(problem, file=sys.stderr)
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
