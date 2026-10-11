# REFERENCES-P.2: References

REFERENCES-P.2, edition 2 of the references of the cint specification, cint v0.2.0, 2026-10-11.

This edition replaces REFERENCES-P.1.

Status: Informative. This edition is the shared bibliography of the editions of the cint specification published with cint v0.2.0: SPEC-00P.2, SPEC-01P.2, SPEC-02P.1, SPEC-03P.1, SPEC-04P.2, SPEC-06P.1 and SPEC-09P.1. The text of this edition does not change after its release. Corrections that change no meaning, such as a locator that has moved, are listed in [ERRATA.md](ERRATA.md); an entry added or removed makes a new edition.

Every published work that a 0.2.0 edition cites is listed here once, with a stable key. An edition lists in its clause 2, Normative references, the works whose content is part of its requirements. It lists the other works it cites in a Bibliography at its end, or names them in the text where they are used. SPEC-00P.2 is informative and has no clause 2; it names works in its text. The editions of the cint specification are not listed here; SPEC-00P.2 section 3 lists them. Each entry ends with a "Cited by" note that names the citing editions by identifier and the clauses that cite the work.

## How to read an entry

- An entry gives the author or organization, the title, the date and a stable identifier: a standard number, an RFC number, a FIPS number, a DOI, an arXiv identifier, an ISBN or the official documentation URL. Titles of whole works are in italics; titles of parts (articles, chapters, pages, sections) are in quotation marks.
- "Read 2026-10-10" marks a web page with no version of its own; the page may change after that date. Where a locator serves the newest version of a versioned document, the entry says so.
- Where an edition names a work without its edition, version or revision, the entry gives the one read on 2026-10-10 and says so.
- In a "Cited by" note, "2" is the citing edition's clause 2, Normative references, and "Bibliography" is its list of other works. Any other number is a section of that edition, followed where it helps by a note, a paragraph or an item.

## How the entries were checked

Each entry's identifier, title, authors or organization, and date were checked on 2026-10-10 against the publisher's own page or registry: Crossref for DOIs, arXiv for arXiv identifiers and the RFC Editor for RFCs.

## 1. Standards and specifications

- [ArrayAPI-2023.12] Consortium for Python Data API Standards. *Python array API standard*, version 2023.12. https://data-apis.org/array-api/2023.12/; the method `array.__dlpack__`: https://data-apis.org/array-api/2023.12/API_specification/generated/array_api.array.__dlpack__.html. The newest version on 2026-10-10 is 2025.12.
  - Cited by: SPEC-03P.1 2 and 8.3.
- [FIPS-180-4] National Institute of Standards and Technology. *Secure Hash Standard (SHS)*. FIPS PUB 180-4, August 2015. DOI 10.6028/NIST.FIPS.180-4.
  - Cited by: SPEC-01P.2 2, 7.6, 8.5 and 9; SPEC-03P.1 2 and 5.1; SPEC-06P.1 2 and 6; SPEC-09P.1 2, 6.5, 10.3 and 11.2.
- [IEEE-754-2019] IEEE. *IEEE Std 754-2019, IEEE Standard for Floating-Point Arithmetic*. IEEE, 2019 (approved 2019-06-13, published 2019-07-22). DOI 10.1109/IEEESTD.2019.8766229.
  - Cited by: SPEC-03P.1 2 and 8.5; SPEC-04P.2 6.7.15, note 12.
- [ISO-1539-1-2023] ISO/IEC JTC 1/SC 22. *ISO/IEC 1539-1:2023, Programming languages: Fortran: Part 1: Base language* (Fortran 2023). International Organization for Standardization, 2023. https://www.iso.org/standard/82170.html. The editions name Fortran without an edition; this is the edition in force on 2026-10-10.
  - Cited by: SPEC-03P.1 8.2, paragraph 13 (Fortran-ordered arrays); SPEC-04P.2 7.2.2, note 11 (`MOD` and `MODULO`), and 7.3, paragraphs 28 and 31 (array element order).
- [ISO-9899-2018] ISO/IEC JTC 1/SC 22. *ISO/IEC 9899:2018, Information technology: Programming languages: C* (C17). International Organization for Standardization, 2018. https://www.iso.org/standard/74528.html. ISO lists this edition as withdrawn on 2024-10-31 and replaced by ISO/IEC 9899:2024; the editions cite the 2018 edition by its date. The WG 14 working paper N2310 (2018-11-06) marks its changes against that edition and can be read without charge: https://www.open-std.org/jtc1/sc22/wg14/www/docs/n2310.pdf.
  - Cited by: SPEC-00P.2 1 and 9; SPEC-02P.1 2 and 10.3; SPEC-03P.1 2 and 7.2; SPEC-04P.2 6.3.2, note 14, Annex C (C.1, which follows Annex J of the standard and names its subclauses, and C.2) and Annex D; SPEC-09P.1 2, 7.2 and 7.4 (subclause 6.5).
- [MISRA-C-2025] The MISRA Consortium Limited. *MISRA C:2025*. March 2025. https://misra.org.uk/misra-c/. The editions name MISRA C without an edition; MISRA C:2025 is the edition current on 2026-10-10.
  - Cited by: SPEC-00P.2 7; SPEC-04P.2 4, LS-275; SPEC-09P.1 11.4, DISC-02.
- [RFC-3339] G. Klyne and C. Newman. *Date and Time on the Internet: Timestamps*. RFC 3339, July 2002. https://www.rfc-editor.org/rfc/rfc3339. Updated by RFC 9557.
  - Cited by: SPEC-06P.1 2 and 12.3.
- [RFC-3629] F. Yergeau. *UTF-8, a transformation format of ISO 10646*. RFC 3629 (STD 63), November 2003. https://www.rfc-editor.org/rfc/rfc3629.
  - Cited by: SPEC-01P.2 2, 5.4, 6.1 and B.1; SPEC-04P.2 2 and 6.4.1; SPEC-06P.1 2; SPEC-09P.1 2, 6.5 and 8.1.
- [RFC-5234] D. Crocker, editor and P. Overell. *Augmented BNF for Syntax Specifications: ABNF*. RFC 5234 (STD 68), January 2008. https://www.rfc-editor.org/rfc/rfc5234. Updated by RFC 7405.
  - Cited by: SPEC-09P.1 2 and 9.3.
- [RFC-7405] P. Kyzivat. *Case-Sensitive String Support in ABNF*. RFC 7405, December 2014. https://www.rfc-editor.org/rfc/rfc7405.
  - Cited by: SPEC-09P.1 2 and 9.3.
- [RFC-7493] T. Bray, editor. *The I-JSON Message Format*. RFC 7493, March 2015. https://www.rfc-editor.org/rfc/rfc7493.
  - Cited by: SPEC-09P.1 2 and 11.2.
- [RFC-8259] T. Bray, editor. *The JavaScript Object Notation (JSON) Data Interchange Format*. RFC 8259 (STD 90), December 2017. https://www.rfc-editor.org/rfc/rfc8259.
  - Cited by: SPEC-01P.2 2 and B.1, paragraph 2 (section 6 of the RFC, on the integers that implementations agree on exactly); SPEC-06P.1 2, 7 and 12.
- [RFC-8785] A. Rundgren, B. Jordan and S. Erdtman. *JSON Canonicalization Scheme (JCS)*. RFC 8785, June 2020. Informational, Independent Submission. https://www.rfc-editor.org/rfc/rfc8785.
  - Cited by: SPEC-06P.1 2 and 12.1; SPEC-09P.1 2 and 11.2.
- [UAX-31] The Unicode Consortium. *Unicode Standard Annex #31: Unicode Identifiers and Syntax*, revision 45, 2026-09-01 (Unicode 18.0.0). https://www.unicode.org/reports/tr31/tr31-45.html (latest version: https://www.unicode.org/reports/tr31/). SPEC-04P.2 names the annex without a revision; revision 45 is the one current on 2026-10-10.
  - Cited by: SPEC-04P.2 Annex E, item 6.
- [UAX-9] The Unicode Consortium. *Unicode Standard Annex #9: Unicode Bidirectional Algorithm*, revision 51, 2025-08-13, the revision of Unicode 17.0.0. https://www.unicode.org/reports/tr9/tr9-51.html.
  - Cited by: SPEC-04P.2 2 and 6.4.1.
- [Unicode-17.0] The Unicode Consortium. *The Unicode Standard, Version 17.0.0*. South San Francisco: The Unicode Consortium, 2025 (released 2025-09-09). ISBN 978-1-936213-35-1. https://www.unicode.org/versions/Unicode17.0.0/.
  - Cited by: SPEC-04P.2 2 and 6.4; SPEC-09P.1 2 and 8.1.

## 2. Vendor, project and tool documentation

- [CERN-ROOT-CINT] CERN, ROOT project. "CINT the C++ Interpreter." Chapter 9 of the ROOT users guide for ROOT 5. https://root.cern/root/html534/guides/users-guide/CINT.html. Read 2026-10-10.
  - Cited by: SPEC-00P.2 8, which names this interpreter to say that cint is not related to it.
- [Clang-ASan] LLVM Project, the Clang Team. "AddressSanitizer." *Clang documentation*. https://clang.llvm.org/docs/AddressSanitizer.html. Read 2026-10-10; the page is the documentation of the development version and names no release.
  - Cited by: SPEC-00P.2 7, claim 9; SPEC-09P.1 7.6, 11.4 and Bibliography.
- [Clang-UBSan] LLVM Project, the Clang Team. "UndefinedBehaviorSanitizer." *Clang documentation*. https://clang.llvm.org/docs/UndefinedBehaviorSanitizer.html. Read 2026-10-10; the page is the documentation of the development version and names no release.
  - Cited by: SPEC-00P.2 7, claim 9; SPEC-09P.1 7.6, 11.4 and Bibliography.
- [Davis-HolyC] Terry A. Davis. "HolyC." TempleOS documentation, file `Doc/HolyC.DD` (undated). No publisher-maintained locator exists; community archive of the TempleOS source: https://github.com/cia-foundation/TempleOS/blob/archive/Doc/HolyC.DD. Read 2026-10-10.
  - Cited by: SPEC-00P.2 9.
- [DLPack] DLPack contributors. *DLPack: Open In Memory Tensor Structure*. Repository https://github.com/dmlc/dlpack; release v1.3 (2026-01-26), the newest on 2026-10-10, whose `include/dlpack/dlpack.h` defines version 1.3. The documentation is at https://dmlc.github.io/dlpack/latest/, including the C API (`c_api.html`) and the Python specification (`python_spec.html`). Its pages are headed "DLPack 0.6.0 documentation." Read 2026-10-10.
  - Cited by: SPEC-03P.1 2, 6.3 and 8.3.
- [GCC-Case-Ranges] Free Software Foundation. "Case Ranges." *Using the GNU Compiler Collection (GCC)*. https://gcc.gnu.org/onlinedocs/gcc/Case-Ranges.html. Read 2026-10-10.
  - Cited by: SPEC-04P.2 Annex D and D.2.
- [GCC-Integer-Overflow-Builtins] Free Software Foundation. "Integer Overflow Builtins." *Using the GNU Compiler Collection (GCC)*. https://gcc.gnu.org/onlinedocs/gcc/Integer-Overflow-Builtins.html. Read 2026-10-10.
  - Cited by: SPEC-09P.1 7.3, paragraph 2, and Bibliography.
- [JSON-Lines] *JSON Lines*. https://jsonlines.org/. Maintained by Ian Ward. A community format description, not a formal standard; the site states that the media type `application/jsonl` is not yet standardized. Read 2026-10-10.
  - Cited by: SPEC-01P.2 2 and B.1.
- [MITRE-CVE-2021-42574] MITRE. *CVE-2021-42574* (bidirectional control characters in source code, "Trojan Source"). CVE record published 2021-11-01. https://www.cve.org/CVERecord?id=CVE-2021-42574.
  - Cited by: SPEC-04P.2 6.4.1, note 7.
- [MITRE-CWE-484] MITRE. "CWE-484: Omitted Break Statement in Switch." *Common Weakness Enumeration*, version 4.20. https://cwe.mitre.org/data/definitions/484.html.
  - Cited by: SPEC-04P.2 C.2.
- [NumPy-shares-memory] NumPy Developers. "numpy.shares_memory." *NumPy Reference* (NumPy v2.5 Manual). https://numpy.org/doc/stable/reference/generated/numpy.shares_memory.html. Read 2026-10-10.
  - Cited by: SPEC-02P.1 6.3, paragraph 4, and Bibliography.
- [NVIDIA-CUDA-Driver-13.4] NVIDIA. *CUDA Driver API Reference Manual*, version 13.4, section 6.27, "Module Management." https://docs.nvidia.com/cuda/cuda-driver-api/ (section 6.27: https://docs.nvidia.com/cuda/cuda-driver-api/cuda_driver_api/group__CUDA__MODULE.html). Read 2026-10-10; the locator serves the newest version.
  - Cited by: SPEC-02P.1 2 and 10.5.
- [NVIDIA-PTX-9.4] NVIDIA. *Parallel Thread Execution ISA*, version 9.4: sections 9.7.1.3 (`mul`), 9.7.2.1 (`add.cc`), 9.7.2.2 (`addc`) and 9.7.15.5 (`atom`). https://docs.nvidia.com/cuda/parallel-thread-execution/. Read 2026-10-10; the locator serves the newest version. CUDA Toolkit 13.4 supports PTX ISA 9.4.
  - Cited by: SPEC-02P.1 2 and 10.5.
- [PEP-3118] Travis Oliphant and Carl Banks. *PEP 3118: Revising the buffer protocol*. Python Enhancement Proposal (Standards Track, status Final), created 2006-08-28. https://peps.python.org/pep-3118/.
  - Cited by: SPEC-03P.1 2 and 8.2.
- [PEP-515] Georg Brandl and Serhiy Storchaka. *PEP 515: Underscores in Numeric Literals*. Python Enhancement Proposal (status Final), created 2016-02-10. https://peps.python.org/pep-0515/.
  - Cited by: SPEC-04P.2 D.1.
- [PSF-CAPI-Buffer] Python Software Foundation. "Buffer Protocol." *Python/C API Reference Manual*, Python 3.14. https://docs.python.org/3.14/c-api/buffer.html.
  - Cited by: SPEC-03P.1 2 and 8.2.
- [PSF-Format-Spec] Python Software Foundation. "Format specification mini-language," in "string: Common string operations." *The Python standard library*, Python 3.14. https://docs.python.org/3.14/library/string.html#formatspec. SPEC-04P.2 cites it without a version.
  - Cited by: SPEC-04P.2 7.5.3.
- [PSF-json] Python Software Foundation. "json: JSON encoder and decoder." *The Python standard library*, Python 3.14. https://docs.python.org/3.14/library/json.html. SPEC-06P.1 names `json.dumps` without a version.
  - Cited by: SPEC-06P.1 12.1, paragraph 3, and 12.6.
- [PSF-LangRef] Python Software Foundation. *The Python Language Reference*, Python 3.14. https://docs.python.org/3.14/reference/. SPEC-04P.2 cites it without a version; https://docs.python.org/3/ serves Python 3.15.0 on 2026-10-10.
  - Cited by: SPEC-04P.2 6.10.4, note 17 (`range`); 6.10.7, note 20 (the `with` statement); 6.11.4, note 11 (`try` and `except`); C.2; Annex D and D.1.
- [PSF-Library] Python Software Foundation. *The Python standard library*, Python 3.14. https://docs.python.org/3.14/library/. SPEC-04P.2 cites it without a version.
  - Cited by: SPEC-04P.2 Annex D.
- [PSF-struct] Python Software Foundation. "struct: Interpret bytes as packed binary data," section "Format Characters." *The Python standard library*, Python 3.14. https://docs.python.org/3.14/library/struct.html.
  - Cited by: SPEC-03P.1 2 and 8.2.
- [pytest-assert] pytest developers. "How to write and report assertions in tests." *pytest documentation*. https://docs.pytest.org/en/stable/how-to/assert.html. Read 2026-10-10.
  - Cited by: SPEC-04P.2 D.1.
- [Rust-std-i32] The Rust Project. "Primitive Type i32." *The Rust Standard Library*, Rust 1.99.0. https://doc.rust-lang.org/std/primitive.i32.html. Read 2026-10-10; the locator serves the newest stable release.
  - Cited by: SPEC-00P.2 9 (`checked_add`).
- [SEI-CERT-C-2016] Software Engineering Institute, Carnegie Mellon University. *SEI CERT C Coding Standard: Rules for Developing Safe, Reliable, and Secure Systems*, 2016 edition. 2016-06-30. https://www.sei.cmu.edu/library/sei-cert-c-coding-standard-rules-for-developing-safe-reliable-and-secure-systems-2016-edition/. The online edition is at https://cmu-sei.github.io/secure-coding-standards/. The editions name the standard without an edition; the 2016 edition is the newest edition published as a whole.
  - Cited by: SPEC-00P.2 7; SPEC-04P.2 4, LS-275; SPEC-09P.1 11.4, DISC-02.
- [Zig-LangRef] Zig Software Foundation. *Zig Language Reference* (master). https://ziglang.org/documentation/master/. Read 2026-10-10; the newest numbered release listed there is 0.17.0.
  - Cited by: SPEC-00P.2 9.

## 3. Papers, books and reports

- [Blelloch-1990] Guy E. Blelloch. *Prefix Sums and Their Applications*. Technical Report CMU-CS-90-190, School of Computer Science, Carnegie Mellon University, November 1990. https://www.cs.cmu.edu/~scandal/papers/CMU-CS-90-190.html.
  - Cited by: SPEC-02P.1 Bibliography, for 9.2 and 10.5.
- [Henriksen-2017] Troels Henriksen, Niels G. W. Serup, Martin Elsman, Fritz Henglein and Cosmin E. Oancea. "Futhark: Purely Functional GPU-Programming with Nested Parallelism and In-Place Array Updates." *Proceedings of the 38th ACM SIGPLAN Conference on Programming Language Design and Implementation (PLDI 2017)*, pages 556-571, 2017. DOI 10.1145/3062341.3062354.
  - Cited by: SPEC-00P.2 9.
- [OCallahan-2017] Robert O'Callahan, Chris Jones, Nathan Froyd, Kyle Huey, Albert Noll and Nimrod Partush. "Engineering Record and Replay for Deployability: Extended Technical Report." arXiv:1705.05937, 2017. https://arxiv.org/abs/1705.05937. The paper describes the rr debugger.
  - Cited by: SPEC-00P.2 9.
- [Pharr-2012] Matt Pharr and William R. Mark. "ispc: A SPMD Compiler for High-Performance CPU Programming." *2012 Innovative Parallel Computing (InPar)*, pages 1-13, May 2012. DOI 10.1109/InPar.2012.6339601.
  - Cited by: SPEC-00P.2 9; SPEC-02P.1 8.2 and Bibliography.
- [Wheeler-2009] David A. Wheeler. *Fully Countering Trusting Trust through Diverse Double-Compiling*. PhD dissertation, George Mason University, 2009. arXiv:1004.5534. https://dwheeler.com/trusting-trust/; repository copy: http://mars.gmu.edu/handle/1920/5667.
  - Cited by: SPEC-00P.2 7, which names the technique; SPEC-09P.1 10.5, 13 and Bibliography.
