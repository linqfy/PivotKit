# Code style

One rule set for the whole codebase (C, C++, Lua, Python, docs).

## Comments

Comments exist to carry technical value the code cannot express. If a comment
only narrates the code, delete it.

* **Do comment:** ABI/convention constraints ("callee cleans stack via ret N"),
  RE-derived facts and their evidence ("offset from vmtInitTable; verified
  against TFigure.Destroy"), ownership/lifetime rules, version-specific
  constants ("5.2.11 only; re-derive per build"), non-obvious gotchas
  ("this Lua's io.popen breaks with a quoted exe").
* **Don't comment:** what the next line does, change history, TODO chatter,
  section banners that repeat the file name, blank-line filler.
* File headers: one to three lines stating what the module is and the key
  constraint it operates under. No author/date/version poetry.
* Confidence markers from the research tree (`[C]`, `[S]`, `[H]`) stay —
  they are data, not prose.

## Naming

* C: `pk_module_function`, types `PkThing`, constants `PK_THING`.
* Lua: `pivotlib.api_name`, locals `snake_case`, no Hungarian suffixes.
* RE-derived names win over style: `TFigure`, `FChildren@+0x40`,
  `0x54A144` stay as-is; the address/offset is part of the name.

## Formatting

* C: 4-space indent, K&R braces, 100-col soft wrap.
* Lua: 4-space indent, `local` by default, globals only at documented API
  boundaries (`_G.pivotlib`, `_G.pl2`).
* Python: PEP 8, stdlib only for tools.

## Markdown

GitHub-flavored. Tables for enumerations, fenced blocks with language tags,
relative links. `docs/README.md` is the docs index; every doc links back to
the thing it documents.
