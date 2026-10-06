# `cint` command: open items

Items of the C17 `cint` command (`cli/`) that wait for a decision or a later task. Each has an entry in `docs/cint/OPEN-QUESTIONS.md`.

- `span` and `function` in `CINT-FAULT-1` (OQ-171). SPEC-04 LS-289 requires every field of the human form in the machine form, including `span`; the `--json` line has neither key yet.

Task 2.15 (`cint build`, `cint test`) leaves these Proposed choices (OQ-181 to OQ-185).

- Tests come from the SIR text (OQ-181). `cint test` finds each test in `<P>.sites` (the `test @<module> <j>` line, then the entry's fuel charge at the `test` token), reads its header from the source, and calls the compiler's `<prefix>_test<j>` entry (compiler/OPEN.md CINTC-OQ-56) through a driver that includes `cint-program.c`. A test table emitted by the compiler would replace the scan; that changes the emitter and resets B1 receipts, so it waits for a unit that can take that cost.
- `CINT-TEST-1`, the machine form of `cint test --json` (OQ-182): one object per test on stdout with `expect_fault`, `expect_line`, `fault` (a `CINT-FAULT-1` object or null), `file`, `fuel_consumed` (exact), `line`, `name` (unescaped), `passed`, and `schema`; no summary line. SPEC-06 15 does not list it yet. The human form is `test <file>:<line> <name as written> ... ok|FAILED`, then `<k> of <n> tests passed`.
- Build output (OQ-183). With no `cint.project` (SPEC-06 3.3) there is one target: FILE.ci, or `main.ci` of a project directory (under `src/` when present). `cint build` writes `<out>/<stem>` (`.exe` on Windows); `--out` defaults to `build` under the project directory or FILE's directory. The build receipt has `method: null` and no stdout digest.
- Discovery (OQ-184). The CLI scans each module's import header (an optional `profile` line, then the imports) to list the modules before the compiler's own discovery pass, which stays the authority: an import the scan does not follow surfaces as a compiler diagnostic. One source root, so each import probes one path; `identity.lookups` maps each probed path to whether it was present (SPEC-09 RCPT-07). At most 2048 modules of 4 MiB each. `cint run` uses the same discovery; its receipt keeps the run keys.
- MSVC object names (OQ-185). `/Fo<dir>\` names each object after its source's file name, so two modules with one file name in different directories (`a.ci`, `lib/a.ci`) collide on the msvc leg. Fixing it means one `/Fo` per module.

Box 10 unit 2 leaves this Proposed choice (box 10 default BX10-07, confirmed 2026-10-05).

- `cint build --lib` (SPEC-06 3.1). The program's modules and the runtime object compiled with `CINT_RT_LIBRARY` (`rt/OPEN.md` RT-OQ-37), named by the toolchain file's `runtime_library_object`, are linked into one shared library, `<out>/lib<stem>.so` (`lib<stem>.dylib` on macOS, `<stem>.dll` on Windows), with `--out` as for a program. A root module with `main` or script statements is a usage error, since a library has no program entry. The build receipt keeps its form.
