# json5-tests

The JSON5 parse cases of [json5/json5-tests](https://github.com/json5/json5-tests), MIT-licensed (`LICENSE.md`), committed for `Json5Tests.cpp`.

- **Source:** https://github.com/json5/json5-tests
- **Commit:** `ceb24d4080137d70833f86c25659c1331b80a387` (2026-02-20)
- **Changes:** none to the cases. Upstream's own `README.md` is kept as `UPSTREAM-README.md`, and this file and `.gitattributes` are added.

Upstream's extensions say what each case is: `.json` and `.json5` are valid and must read; `.js` and `.txt` are invalid and must fail. `.errorSpec` files are json5's reference reader's locations for some invalid cases; `Json5Tests.cpp` holds the locations this reader is held to.

The files are kept byte for byte as upstream stores them (`.gitattributes` marks them binary), so the `new-lines` cases keep their CR, LF and CRLF line endings. To update, take a new commit's tree with `git -c core.autocrlf=false archive`, which keeps those bytes, and name the commit here.
