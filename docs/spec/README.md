# CINT specification

The CINT specification defines the language, its integer machine, and its toolchain. It is published in editions, and an edition does not change after the release that first publishes it.

## Editions

An edition is named `SPEC-NNP.E`: the specification number `NN`, `P` for public, and the edition number `E`. `SPEC-01P.1` is the first public edition of SPEC-01, the integer machine. The glossary and the references follow the same scheme, as `GLOSSARY-P.E` and `REFERENCES-P.E`.

- **Frozen text.** Once released, an edition's text does not change. A correction that changes no meaning (a typo, a broken link, a clearer sentence) is listed in [ERRATA.md](ERRATA.md) as an erratum, written `SPEC-01P.1 E1`, and the edition keeps its number. A change of meaning (a rule, a value, a fault code, a clause added or removed, a Proposed rule becoming Specified) is published as a new edition, beside the earlier one.
- **Numbered per specification.** Each specification counts its own editions, so SPEC-01P can be at edition 3 while SPEC-04P is at edition 1. Each edition first appears in exactly one release.
- **Stable identifiers.** Clause identifiers such as `IM-106` and `LS-17`, section numbers, and open-question identifiers such as `OQ-15` are the same in every edition and are never reused. A citation such as `SPEC-04 LS-17`, in a source comment or a conformance case, refers to that clause in the current edition of SPEC-04P.
- **Status labels.** Each rule is labeled Specified, Proposed, or Open. The language profile `cint-core-1` is a draft and is not yet frozen, so a Proposed rule may change in a later edition.
- **Not here.** What a given release implements, and what its compiler refuses, is stated in that release's notes.

## Current editions

| Edition | Title | First released in |
| --- | --- | --- |
| [SPEC-00P.1](SPEC-00P.1-OVERVIEW.md) | Overview | cint v0.1.0 |
| [SPEC-01P.1](SPEC-01P.1-INTEGER-MACHINE.md) | Integer machine | cint v0.1.0 |
| [SPEC-04P.1](SPEC-04P.1-LANGUAGE-SURFACE.md) | Language surface | cint v0.1.0 |
| [GLOSSARY-P.1](GLOSSARY-P.1.md) | Glossary | cint v0.1.0 |
| [REFERENCES-P.1](REFERENCES-P.1.md) | References | cint v0.1.0 |

## Specifications without a public edition

The published editions cite these by clause identifier. They have no public text yet.

| Number | Title |
| --- | --- |
| SPEC-02 | Kernels and views |
| SPEC-03 | Memory, host boundary, and interop |
| SPEC-05 | Ternary representations and kernels |
| SPEC-06 | Workbench, debugger, and tooling |
| SPEC-07 | Security model |
| SPEC-08 | Bolt-under integration: HLA, SpaceFOM, Trick, Fortran |
| SPEC-09 | Compiler architecture, bootstrap, and self-hosting |

## Editions by release

| Release | Editions |
| --- | --- |
| cint v0.1.0 | SPEC-00P.1, SPEC-01P.1, SPEC-04P.1, GLOSSARY-P.1, REFERENCES-P.1 |
