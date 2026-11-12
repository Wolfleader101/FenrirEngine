# 13: Text entry

**What to build:** a single-line text entry a player can type, select, cut, copy and paste in, with the OS text input and candidate window placed at its caret (spec: "FenrirUI" controls (text entry); ADR-0066's edit model).

- UTF-8 value; caret and anchor as byte offsets on grapheme boundaries from FenrirText; arrows, Home, End, Shift to extend, Ctrl with arrows by word (UAX#29 words), Backspace and Delete (whole graphemes), Ctrl+A, Ctrl+C, Ctrl+X, Ctrl+V through FenrirWindow's clipboard, Enter sends submit, Escape blurs; click places the caret at the hit character, drag selects, double-click selects a word; committed text arrives through FenrirInput's `TextEvent`; while focused the root's router publishes the text-input claim with the caret's rectangle in logical units so FenrirWindow turns SDL's text input on and the OS places any IME window; the caret blinks on FenrirTime; the view scrolls horizontally to keep the caret visible; `placeholder`, `password` masking with a bullet, `maxLength`; sends change on edits.
- The caret and selection draw through the painter under the sheet's colours.

**Blocked by:** 09, 11

**Touches:** `packages/FenrirUI/`

**Status:** resolved

- [x] Headless App tests with the test feed: typing through text events inserts at the caret; a combining sequence deletes as one grapheme; Ctrl+arrow jumps words; Shift+End selects to the end and typing replaces the selection; Ctrl+C then Ctrl+V duplicates through the clipboard (skipped without a display, as FenrirWindow's clipboard tests are); Enter sends submit with the value, Escape blurs; the claims resource shows a text-input claim with a caret rectangle inside the entry while focused and none after blur; max length stops input; password mode measures bullets.
- [x] Draw-list test: the selection rectangle and caret draw in the expected positions for a known string.
- [x] All passes under the msvc-x64-debug preset; `check_tickets.py` and clang-format pass.
