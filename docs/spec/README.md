# cint specification

The cint specification defines the cint language, the encodings of its values and records, its kernels and executors, its host interface, its tools and its toolchain. It is published in editions, and an edition does not change after the release that first publishes it.

Start with [SPEC-00P.2](SPEC-00P.2-OVERVIEW.md), the overview: section 3 describes each document and section 4 gives reading orders for a learner, a C programmer, a Python user and an implementer. A reader learning the language needs only [SPEC-04P.4](SPEC-04P.4-LANGUAGE.md).

## Editions

An edition is named `SPEC-NNP.E`: the document number `NN`, `P` for public and the edition number `E`. SPEC-04P.2 is the second edition of SPEC-04, the language. The glossary and the references follow the same scheme, as `GLOSSARY-P.E` and `REFERENCES-P.E`.

- **Frozen text.** A released edition's text does not change. A correction that changes no meaning (a typo, a broken link, a clearer sentence) is listed in [ERRATA.md](ERRATA.md), written `SPEC-04P.2 E1`, and the edition keeps its number. A change of meaning is published as a new edition, beside the earlier one.
- **Releases.** Each minor release, such as 0.2.0, publishes an edition of every document. A patch release, such as 0.2.1, publishes the next edition only of a document whose rules or listed refusals it changes.
- **Numbered per document.** Each document counts its own editions, so SPEC-04P can be at edition 3 while SPEC-02P is at edition 1.
- **Stable identifiers.** Each rule ends with its clause identifier, such as `IM-106` or `LS-17`. An identifier stays with its rule when the rule moves to another section or document, and it is never reused. Section numbers belong to one edition. Every rule has an anchor named by its identifier and every section an anchor named by its title, so a link keeps working in each edition that keeps the rule or the section.
- **Citations from tools.** A diagnostic or a fault report may cite a rule as `SPEC-04 LS-17`, by document number and identifier. The rule is in the current edition of that document, at the anchor `#LS-17`; where another document now states it, the cited document lists the identifier with a link to the edition and section that state it.
- **What the editions state.** The rules of an edition are what the reference of its release runs. The constructs that the other tools of that release refuse are listed, with their diagnostic codes, in SPEC-09P.3 clause 12.

## Current editions

| Edition | Title | First released in |
| --- | --- | --- |
| [SPEC-00P.2](SPEC-00P.2-OVERVIEW.md) | Overview | cint v0.2.0 |
| [SPEC-01P.3](SPEC-01P.3-ENCODINGS.md) | Canonical encodings and identity | cint v0.2.3 |
| [SPEC-02P.2](SPEC-02P.2-KERNELS.md) | Kernels and executors | cint v0.2.3 |
| [SPEC-03P.3](SPEC-03P.3-HOST-INTERFACE.md) | The host interface: C and Python | cint v0.2.4 |
| [SPEC-04P.4](SPEC-04P.4-LANGUAGE.md) | The cint language | cint v0.2.4 |
| [SPEC-06P.3](SPEC-06P.3-TOOLS.md) | The cint tools | cint v0.2.4 |
| [SPEC-09P.3](SPEC-09P.3-TOOLCHAIN.md) | Toolchain and conformance | cint v0.2.4 |
| [GLOSSARY-P.4](GLOSSARY-P.4.md) | Glossary | cint v0.2.4 |
| [REFERENCES-P.2](REFERENCES-P.2.md) | References | cint v0.2.0 |

The numbers SPEC-05, SPEC-07 and SPEC-08 have no public edition.

## Earlier editions

An earlier edition stays here unchanged, so that a link to it keeps working. Its rules are those of the release it was published with.

| Edition | Title | Replaced by |
| --- | --- | --- |
| [SPEC-03P.2](SPEC-03P.2-HOST-INTERFACE.md) | The host interface: C and Python | SPEC-03P.3 |
| [SPEC-04P.3](SPEC-04P.3-LANGUAGE.md) | The cint language | SPEC-04P.4 |
| [SPEC-06P.2](SPEC-06P.2-TOOLS.md) | The cint tools | SPEC-06P.3 |
| [SPEC-09P.2](SPEC-09P.2-TOOLCHAIN.md) | Toolchain and conformance | SPEC-09P.3 |
| [GLOSSARY-P.3](GLOSSARY-P.3.md) | Glossary | GLOSSARY-P.4 |
| [SPEC-01P.2](SPEC-01P.2-ENCODINGS.md) | Canonical encodings and identity | SPEC-01P.3 |
| [SPEC-02P.1](SPEC-02P.1-KERNELS.md) | Kernels and executors | SPEC-02P.2 |
| [SPEC-03P.1](SPEC-03P.1-HOST-INTERFACE.md) | The host interface: C and Python | SPEC-03P.2 |
| [SPEC-04P.2](SPEC-04P.2-LANGUAGE.md) | The cint language | SPEC-04P.3 |
| [SPEC-06P.1](SPEC-06P.1-TOOLS.md) | The cint tools | SPEC-06P.2 |
| [SPEC-09P.1](SPEC-09P.1-TOOLCHAIN.md) | Toolchain and conformance | SPEC-09P.2 |
| [GLOSSARY-P.2](GLOSSARY-P.2.md) | Glossary | GLOSSARY-P.3 |
| [SPEC-00P.1](SPEC-00P.1-OVERVIEW.md) | Overview | SPEC-00P.2 |
| [SPEC-01P.1](SPEC-01P.1-INTEGER-MACHINE.md) | Integer machine | SPEC-01P.2 and SPEC-04P.2 |
| [SPEC-04P.1](SPEC-04P.1-LANGUAGE-SURFACE.md) | Language surface | SPEC-04P.2 |
| [GLOSSARY-P.1](GLOSSARY-P.1.md) | Glossary | GLOSSARY-P.2 |
| [REFERENCES-P.1](REFERENCES-P.1.md) | References | REFERENCES-P.2 |

## Editions by release

| Release | Editions |
| --- | --- |
| cint v0.2.4 | SPEC-03P.3, SPEC-04P.4, SPEC-06P.3, SPEC-09P.3, GLOSSARY-P.4 |
| cint v0.2.3 | SPEC-01P.3, SPEC-02P.2, SPEC-03P.2, SPEC-04P.3, SPEC-06P.2, SPEC-09P.2, GLOSSARY-P.3 |
| cint v0.2.0 | SPEC-00P.2, SPEC-01P.2, SPEC-02P.1, SPEC-03P.1, SPEC-04P.2, SPEC-06P.1, SPEC-09P.1, GLOSSARY-P.2, REFERENCES-P.2 |
| cint v0.1.0 | SPEC-00P.1, SPEC-01P.1, SPEC-04P.1, GLOSSARY-P.1, REFERENCES-P.1 |
