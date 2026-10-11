# cli/OPEN.md: open items

Known limitations of the `cint` command (`cli/`) at this release, and choices that may still change.

## The machine fault form has no `span` or `function`

SPEC-04 LS-289 requires every field of the human fault form in the machine form. The `--json` fault line (`CINT-FAULT-1`) does not carry `span` or `function` yet.

## Tests are read from the SIR text

`cint test` finds each test in the compiler's SIR text: the `test @<module> <j>` line of `<P>.sites`, then the entry's fuel charge at the `test` token. It reads the test's header from the source and calls the compiler's `<prefix>_test<j>` entry through a driver that includes `cint-program.c`. A test table written by the compiler would replace this scan.

## `cint test --json`

`CINT-TEST-1`, the machine form of `cint test --json`, writes one object per test on stdout with the keys `expect_fault`, `expect_line`, `fault` (a `CINT-FAULT-1` object or null), `file`, `fuel_consumed` (exact), `line`, `name` (unescaped), `passed` and `schema`, and no summary line. SPEC-06 15 does not list it yet. The human form is `test <file>:<line> <name as written> ... ok|FAILED`, then `<k> of <n> tests passed`.

## Build output

There is no project file yet (SPEC-06 3.3), so a build has one target: FILE.ci, or `main.ci` of a project directory (under `src/` when present). `cint build` writes `<out>/<stem>` (`<stem>.exe` on Windows). `--out` defaults to `build` under the project directory or FILE's directory. The build receipt has `method: null` and no stdout digest.

`cint build --lib` (SPEC-06 3.1) links the program's modules and a runtime object compiled as a library into one shared library: `<out>/lib<stem>.so`, `lib<stem>.dylib` on macOS, `<stem>.dll` on Windows. A root module with `main` or script statements is a usage error, since a library has no program entry.

## Module discovery

The command reads each module's import header (an optional `profile` line, then the imports) to list the modules before the compiler's own discovery, which decides: an import the command does not follow is reported by the compiler. There is one source root, so each import probes one path, and `identity.lookups` maps each probed path to whether it was present (SPEC-09 RCPT-07). A program has at most 2,048 modules of at most 4 MiB each. `cint run` discovers modules the same way.

## Object names with MSVC

With MSVC, `/Fo<dir>\` names each object after its source file, so two modules with the same file name in different directories (`a.ci` and `lib/a.ci`) collide. GCC, Clang and Apple Clang are not affected.
