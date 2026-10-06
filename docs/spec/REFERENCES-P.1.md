# REFERENCES-P.1: References

REFERENCES-P.1, edition 1 of the references of the CINT specification, cint v0.1.0, 2026-10-06.

Status: Draft. The shared bibliography of the published editions of the CINT specification. The text of this edition does not change after its release. Corrections that change no meaning, such as a locator that has moved, are listed in [ERRATA.md](ERRATA.md); an entry added or removed makes a new edition.

Every work that a published edition cites is listed here once, with a stable key. Each edition keeps its own numbered `## References` list, cited inline as `[n]` in order of first appearance. An edition's entry repeats the entry below word for word, starting with its key in brackets, and may add one "Used here" note that names the sections, clauses, or files that edition relies on. Section numbers follow the internal bibliography, so a section with no entry in this edition keeps its number.

## How to read an entry

- An entry gives the author or organization, the title, the date, and a stable identifier: a standard number, an RFC number, a FIPS number, a DOI, an arXiv identifier, an ISBN, or the official documentation URL. Titles of whole works are in italics; titles of parts (articles, chapters, pages) are in quotation marks.
- "Read 2026-10-02" marks a web page with no version of its own; the page may change after that date.
- An entry that could not be confirmed says "Unverified" and gives the reason. Section 7 lists them.

## What "verified" means

Every entry was checked on 2026-10-02 or on the date in its row of section 6. For each entry, the identifier resolved to the work, and the title, the authors or organization, and the date matched; DOIs were checked against Crossref metadata, arXiv identifiers against arXiv, and web pages by fetching them. Where an entry or a "Used here" note quotes a work, the fetched text was searched for the quotation, and section 6 says so. Not every clause that an edition cites was re-read: the remaining gaps are listed in the open questions of the citing edition. Paywalled standards (ISO, IEEE) were confirmed by catalog page and number, not by reading the purchased text.

Totals: 76 entries, 75 verified and 1 unverified.

## 1. Standards and specifications

- [FIPS-180-4] National Institute of Standards and Technology. *Secure Hash Standard (SHS)*. FIPS PUB 180-4, August 2015. DOI 10.6028/NIST.FIPS.180-4.
- [IEEE-1516-2010] IEEE. *IEEE Std 1516-2010, IEEE Standard for Modeling and Simulation (M&S) High Level Architecture (HLA): Framework and Rules*. IEEE, 2010. DOI 10.1109/IEEESTD.2010.5553440. IEEE lists this edition as Inactive-Reserved since 2021-03-25 and superseded by IEEE 1516-2025 (HLA 4), https://standards.ieee.org/ieee/1516/6687/.
- [IEEE-1516.1-2010] IEEE. *IEEE Std 1516.1-2010, IEEE Standard for Modeling and Simulation (M&S) High Level Architecture (HLA): Federate Interface Specification*. IEEE, 2010. DOI 10.1109/IEEESTD.2010.5557728. IEEE lists this edition as Inactive-Reserved since 2021-03-25 and superseded by IEEE 1516.1-2025.
- [IEEE-1516.2-2010] IEEE. *IEEE Std 1516.2-2010, IEEE Standard for Modeling and Simulation (M&S) High Level Architecture (HLA): Object Model Template (OMT) Specification*. IEEE, 2010. DOI 10.1109/IEEESTD.2010.5557731. IEEE lists this edition as Inactive-Reserved since 2021-03-25 and superseded by IEEE 1516.2-2025.
- [IEEE-754-2019] IEEE. *IEEE Std 754-2019, IEEE Standard for Floating-Point Arithmetic*. IEEE, 2019. DOI 10.1109/IEEESTD.2019.8766229.
- [ISO-1539-1-2018] ISO/IEC JTC 1/SC 22. *ISO/IEC 1539-1:2018, Information technology: Programming languages: Fortran: Part 1: Base language* (Fortran 2018). International Organization for Standardization, 2018. https://www.iso.org/standard/72320.html. ISO has replaced it with ISO/IEC 1539-1:2023 (Fortran 2023). Final committee draft: J3/18-007r1, https://j3-fortran.org/doc/year/18/18-007r1.pdf.
- [ISO-9899-2018] ISO/IEC JTC 1/SC 22. *ISO/IEC 9899:2018, Information technology: Programming languages: C* (C17). International Organization for Standardization, 2018. https://www.iso.org/standard/74528.html. ISO has replaced it with ISO/IEC 9899:2024. Working drafts with the same clause numbering: WG 14 N2176 (ballot text) and N2310, https://www.open-std.org/jtc1/sc22/wg14/www/docs/n2310.pdf.
- [ISO-9899-2024] ISO/IEC JTC 1/SC 22. *ISO/IEC 9899:2024, Information technology: Programming languages: C* (C23). International Organization for Standardization, 2024. https://www.iso.org/standard/82075.html.
- [MISRA-C-2025] MISRA Consortium. *MISRA C:2025, Guidelines for the use of the C language in critical systems*. The MISRA Consortium, 2025. https://misra.org.uk/misra-c/.
- [RFC-3629] F. Yergeau. *UTF-8, a transformation format of ISO 10646*. RFC 3629 (STD 63), November 2003. https://www.rfc-editor.org/rfc/rfc3629.
- [RFC-8032] S. Josefsson and I. Liusvaara. *Edwards-Curve Digital Signature Algorithm (EdDSA)*. RFC 8032, January 2017. https://www.rfc-editor.org/rfc/rfc8032.
- [RFC-8259] T. Bray, editor. *The JavaScript Object Notation (JSON) Data Interchange Format*. RFC 8259 (STD 90), December 2017. https://www.rfc-editor.org/rfc/rfc8259.
- [SISO-STD-018-2020] Simulation Interoperability Standards Organization (SISO). *SISO-STD-018-2020, Standard for Space Reference Federation Object Model (SpaceFOM)*, version 1.0 (document dated 25 October 2019; approved by the SISO Standards Activity Committee on 18 December 2019 and by the SISO Executive Committee on 9 January 2020). SISO, 2020. https://cdn.ymaws.com/www.sisostandards.org/resource/resmgr/standards_products/siso-std-018-2020_srfom.pdf.
- [UAX-31] The Unicode Consortium. *Unicode Standard Annex #31: Unicode Identifiers and Syntax*, revision 45, 2026-09-01. https://www.unicode.org/reports/tr31/.
- [UAX-44] The Unicode Consortium. *Unicode Standard Annex #44: Unicode Character Database*, revision 38, 2026-09-02. https://www.unicode.org/reports/tr44/.
- [UAX-9] The Unicode Consortium. *Unicode Standard Annex #9: Unicode Bidirectional Algorithm*, revision 52, 2026-09-01. https://www.unicode.org/reports/tr9/.
- [Unicode-17.0] The Unicode Consortium. *The Unicode Standard, Version 17.0.0*. The Unicode Consortium, 2025. https://www.unicode.org/versions/Unicode17.0.0/.
- [Unicode-18.0] The Unicode Consortium. *The Unicode Standard, Version 18.0.0*. The Unicode Consortium, 2026. ISBN 978-1-936213-36-8. https://www.unicode.org/versions/Unicode18.0.0/.
- [UTS-39] The Unicode Consortium. *Unicode Technical Standard #39: Unicode Security Mechanisms*, revision 34, 2026-08-27. https://www.unicode.org/reports/tr39/.
- [W3C-WebGPU] W3C GPU for the Web Working Group. *WebGPU*. W3C Candidate Recommendation Draft, 15 September 2026. https://www.w3.org/TR/2026/CRD-webgpu-20260915/ (latest version: https://www.w3.org/TR/webgpu/).
- [W3C-WGSL] W3C GPU for the Web Working Group. *WebGPU Shading Language*. W3C Candidate Recommendation Draft, 21 September 2026. https://www.w3.org/TR/2026/CRD-WGSL-20260921/ (latest version: https://www.w3.org/TR/WGSL/).

## 2. Vendor, project, and tool documentation

- [Arm-AAPCS64] Arm Limited. *Procedure Call Standard for the Arm 64-bit Architecture (AArch64)* (AAPCS64), release 2025Q4, 23 January 2026. https://github.com/ARM-software/abi-aa/blob/main/aapcs64/aapcs64.rst.
- [Arm-CMSIS-DSP] Arm Limited. "Vector Scale." *CMSIS-DSP* documentation. https://arm-software.github.io/CMSIS-DSP/latest/group__BasicScale.html (legacy CMSIS 5 text: https://arm-software.github.io/CMSIS_5/DSP/html/group__BasicScale.html). Read 2026-10-02; the page states no library version.
- [Clang-LangExt] LLVM Project. "Clang Language Extensions." *Clang documentation*. https://clang.llvm.org/docs/LanguageExtensions.html. Read 2026-10-02.
- [Davis-HolyC] Terry A. Davis. "HolyC." TempleOS documentation, file `Doc/HolyC.DD` (undated). No publisher-maintained locator exists; community archive of the TempleOS source: https://github.com/cia-foundation/TempleOS/blob/archive/Doc/HolyC.DD. Read 2026-10-02.
- [DLPack] DLPack contributors. *DLPack: Open In Memory Tensor Structure*. Repository https://github.com/dmlc/dlpack (newest release v1.3, 2026-01-26; v1.0 released 2024-09-09). Documentation https://dmlc.github.io/dlpack/latest/, including the C API (`c_api.html`) and the Python specification (`python_spec.html`); the documentation pages are headed "DLPack 0.6.0 documentation." Read 2026-10-02.
- [GCC-Case-Ranges] Free Software Foundation. "Case Ranges." *Using the GNU Compiler Collection (GCC)*. https://gcc.gnu.org/onlinedocs/gcc/Case-Ranges.html. Read 2026-10-02.
- [GCC-Code-Gen-Options] Free Software Foundation. "Options for Code Generation Conventions." *Using the GNU Compiler Collection (GCC)*. https://gcc.gnu.org/onlinedocs/gcc/Code-Gen-Options.html. Read 2026-10-02.
- [GCC-int128] Free Software Foundation. "128-bit Integers" (`__int128`). *Using the GNU Compiler Collection (GCC)*. https://gcc.gnu.org/onlinedocs/gcc/_005f_005fint128.html. Read 2026-10-02.
- [GCC-Integer-Overflow-Builtins] Free Software Foundation. "Integer Overflow Builtins." *Using the GNU Compiler Collection (GCC)*. https://gcc.gnu.org/onlinedocs/gcc/Integer-Overflow-Builtins.html. Read 2026-10-02.
- [Intel-SDM] Intel Corporation. *Intel 64 and IA-32 Architectures Software Developer's Manual* (Volume 1: Basic Architecture; Combined Volumes 2A to 2D: Instruction Set Reference, order number 325383). Updated 21 September 2026. https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html.
- [JSON-Lines] *JSON Lines*. https://jsonlines.org/. A community format description, not a formal standard; the site states that the media type `application/jsonl` is not yet standardized. Read 2026-10-02.
- [Jupyter-Notebook] Project Jupyter. "The Jupyter Notebook." *Jupyter Notebook documentation*. https://jupyter-notebook.readthedocs.io/en/stable/notebook.html. Read 2026-10-02.
- [Khronos-Vulkan] The Khronos Vulkan Working Group. *Vulkan API Specification* (Vulkan 1.4.365 with all registered extensions on 2026-10-02). The Khronos Group. https://registry.khronos.org/vulkan/specs/latest/html/vkspec.html; chapters and reference pages at https://docs.vulkan.org/. Read 2026-10-02.
- [MITRE-CVE-2021-42574] MITRE. *CVE-2021-42574* (bidirectional control characters in source code, "Trojan Source"). CVE record published 2021-11-01. https://www.cve.org/CVERecord?id=CVE-2021-42574.
- [MITRE-CVE-2021-42694] MITRE. *CVE-2021-42694* (homoglyphs in source code, "Trojan Source"). CVE record published 2021-11-01. https://www.cve.org/CVERecord?id=CVE-2021-42694.
- [MITRE-CWE-484] MITRE. "CWE-484: Omitted Break Statement in Switch." *Common Weakness Enumeration*, version 4.20. https://cwe.mitre.org/data/definitions/484.html.
- [MS-CSharp-Constant-Expressions] Microsoft. "Expressions," section 12.26, "Constant expressions." *C# language specification*, *Microsoft Learn*. https://learn.microsoft.com/en-us/dotnet/csharp/language-reference/language-specification/expressions#1226-constant-expressions. Read 2026-10-03. The ECMA-334 working drafts number the section 12.25.
- [MS-CSharp-Integer-Literals] Microsoft. "Lexical structure," section 6.4.5.3, "Integer literals." *C# language specification*, *Microsoft Learn*. https://learn.microsoft.com/en-us/dotnet/csharp/language-reference/language-specification/lexical-structure#6453-integer-literals. Read 2026-10-03.
- [MS-CSharp-Lexical-General] Microsoft. "Lexical structure," section 6.3.1, "General." *C# language specification*, *Microsoft Learn*. https://learn.microsoft.com/en-us/dotnet/csharp/language-reference/language-specification/lexical-structure#631-general. Read 2026-10-03 (source: `dotnet/csharpstandard` at `cc01bf4`, `standard/lexical-structure.md`).
- [MS-CSharp-Named-Args] Microsoft. "Named and Optional Arguments (C# Programming Guide)." *Microsoft Learn*. https://learn.microsoft.com/en-us/dotnet/csharp/programming-guide/classes-and-structs/named-and-optional-arguments. Read 2026-10-02.
- [MS-x64-Conventions] Microsoft. "x64 ABI conventions." *Microsoft Learn*. https://learn.microsoft.com/en-us/cpp/build/x64-software-conventions. Read 2026-10-02.
- [NASA-Trick] NASA Johnson Space Center. *Trick Simulation Environment*. https://github.com/nasa/trick. Newest release 25.1.1, 2026-09-04.
- [NASA-TrickHLA] NASA Johnson Space Center, Simulation and Graphics Branch. *TrickHLA: IEEE 1516 High Level Architecture simulation interoperability middleware for the Trick Simulation Environment*. NASA Open Source Agreement 1.3. https://github.com/nasa/TrickHLA. Release v3.2.2, 2026-04-01.
- [NIST-CAVP-SHA-Vectors] National Institute of Standards and Technology, Cryptographic Algorithm Validation Program. SHA test vectors for byte-oriented messages (`shabytetestvectors.zip`). https://csrc.nist.gov/CSRC/media/Projects/Cryptographic-Algorithm-Validation-Program/documents/shs/shabytetestvectors.zip.
- [NumPy-Interop] NumPy Developers. "Interoperability with NumPy." *NumPy User Guide*. https://numpy.org/doc/stable/user/basics.interoperability.html. Read 2026-10-02.
- [PEP-3118] Travis Oliphant and Carl Banks. *PEP 3118: Revising the buffer protocol*. Python Enhancement Proposal (status Final), 2006. https://peps.python.org/pep-3118/.
- [PEP-515] Georg Brandl and Serhiy Storchaka. *PEP 515: Underscores in Numeric Literals*. Python Enhancement Proposal, 2016. https://peps.python.org/pep-0515/.
- [PSF-builtins] Python Software Foundation. "builtins: Built-in objects." *The Python Standard Library*, Python 3.14. https://docs.python.org/3/library/builtins.html. Read 2026-10-03.
- [PSF-fractions] Python Software Foundation. "fractions: Rational numbers." *The Python Standard Library*, Python 3.14. https://docs.python.org/3/library/fractions.html.
- [PSF-Functions-Format] Python Software Foundation. "Built-in Functions" and "string: Common string operations" (Format Specification Mini-Language). *The Python Standard Library*, Python 3.14. https://docs.python.org/3/library/functions.html and https://docs.python.org/3/library/string.html#formatspec.
- [PSF-LangRef] Python Software Foundation. *The Python Language Reference*, Python 3.14. https://docs.python.org/3/reference/.
- [PSF-stdtypes] Python Software Foundation. "Built-in Types." *The Python Standard Library*, Python 3.14. https://docs.python.org/3/library/stdtypes.html.
- [PSF-WhatsNew-3.8] Python Software Foundation. "What's New In Python 3.8." 2019. https://docs.python.org/3/whatsnew/3.8.html.
- [pytest-assert] pytest developers. "How to write and report assertions in tests." *pytest documentation*. https://docs.pytest.org/en/stable/how-to/assert.html. Read 2026-10-02.
- [RB-Definitions] Reproducible Builds project. "Definitions." https://reproducible-builds.org/docs/definition/. Read 2026-10-02.
- [SEI-CERT-C-2016] Software Engineering Institute, Carnegie Mellon University. *SEI CERT C Coding Standard: Rules for Developing Safe, Reliable, and Secure Systems*, 2016 edition. 2016. https://www.sei.cmu.edu/library/sei-cert-c-coding-standard-rules-for-developing-safe-reliable-and-secure-systems-2016-edition/.
- [SEI-CERT-INT02-C] Software Engineering Institute, Carnegie Mellon University. "INT02-C. Understand integer conversion rules." *SEI CERT C Coding Standard* (online edition). https://wiki.sei.cmu.edu/confluence/spaces/c/pages/87152206/INT02-C.+Understand+integer+conversion+rules. Unverified: on 2026-10-02 this locator redirected to a page-not-found on the standard's new site, https://cmu-sei.github.io/secure-coding-standards/, whose index still lists INT02-C.
- [Swift-Book] Apple Inc. and the Swift project. "Functions" (section "Function Argument Labels and Parameter Names"). *The Swift Programming Language*, source file `TSPL.docc/LanguageGuide/Functions.md`. https://github.com/swiftlang/swift-book/blob/main/TSPL.docc/LanguageGuide/Functions.md. Read 2026-10-02.
- [SysV-AMD64-ABI] x86-64 psABI project. *System V Application Binary Interface: AMD64 Architecture Processor Supplement*, current version. https://gitlab.com/x86-psABIs/x86-64-ABI. Read 2026-10-02.
- [TI-SPRA109] J. Stevenson. *Q-Values in the Watch Window*. Application Report SPRA109, Texas Instruments, February 2002. https://www.ti.com/lit/an/spra109/spra109.pdf.
- [Wasmtime-Deterministic] Wasmtime project. "Deterministic Execution." *Wasmtime documentation*. https://docs.wasmtime.dev/examples-deterministic-wasm-execution.html. Read 2026-10-03. The page does not name its publisher in its text.
- [Wasmtime-Interrupting] Wasmtime project. "Interrupting Execution." *Wasmtime documentation*. https://docs.wasmtime.dev/examples-interrupting-wasm.html. Read 2026-10-03. The page does not name its publisher in its text; it links to the repository `github.com/bytecodealliance/wasmtime`.
- [Zig-LangRef] Zig Software Foundation. *Zig Language Reference* (master). https://ziglang.org/documentation/master/. Read 2026-10-02.

## 3. Papers, books, and reports

- [Blelloch-1990] Guy E. Blelloch. *Prefix Sums and Their Applications*. Technical Report CMU-CS-90-190, School of Computer Science, Carnegie Mellon University, November 1990. http://www.cs.cmu.edu/afs/cs.cmu.edu/project/scandal/public/papers/CMU-CS-90-190.html.
- [Boucher-2021] Nicholas Boucher and Ross Anderson. "Trojan Source: Invisible Vulnerabilities." arXiv:2111.00169, 2021 (revised 2023). https://arxiv.org/abs/2111.00169.
- [Boute-1992] Raymond T. Boute. "The Euclidean definition of the functions div and mod." *ACM Transactions on Programming Languages and Systems* 14(2):127-144, 1992. DOI 10.1145/128861.128862.
- [Charlesworth-1981] Alan E. Charlesworth. "An Approach to Scientific Array Processing: The Architectural Design of the AP-120B/FPS-164 Family." *Computer* 14(9):18-27, IEEE, September 1981. DOI 10.1109/C-M.1981.220595.
- [Henriksen-2017] Troels Henriksen, Niels G. W. Serup, Martin Elsman, Fritz Henglein, and Cosmin E. Oancea. "Futhark: Purely Functional GPU-Programming with Nested Parallelism and In-Place Array Updates." *Proceedings of the 38th ACM SIGPLAN Conference on Programming Language Design and Implementation (PLDI 2017)*, pages 556-571, 2017. DOI 10.1145/3062341.3062354.
- [Knuth-1997] Donald E. Knuth. *The Art of Computer Programming, Volume 2: Seminumerical Algorithms*, 3rd edition. Addison-Wesley, 1997. ISBN 978-0-201-89684-8.
- [Ma-2024] Shuming Ma, Hongyu Wang, Lingxiao Ma, Lei Wang, Wenhui Wang, Shaohan Huang, Li Dong, Ruiping Wang, Jilong Xue, and Furu Wei. "The Era of 1-bit LLMs: All Large Language Models are in 1.58 Bits." arXiv:2402.17764, 2024. https://arxiv.org/abs/2402.17764.
- [OCallahan-2017] Robert O'Callahan, Chris Jones, Nathan Froyd, Kyle Huey, Albert Noll, and Nimrod Partush. "Engineering Record and Replay for Deployability: Extended Technical Report." arXiv:1705.05937, 2017. https://arxiv.org/abs/1705.05937.
- [Pharr-2012] Matt Pharr and William R. Mark. "ispc: A SPMD Compiler for High-Performance CPU Programming." *2012 Innovative Parallel Computing (InPar)*, pages 1-13, 2012. DOI 10.1109/InPar.2012.6339601.
- [RaganKelley-2013] Jonathan Ragan-Kelley, Connelly Barnes, Andrew Adams, Sylvain Paris, Frédo Durand, and Saman Amarasinghe. "Halide: A Language and Compiler for Optimizing Parallelism, Locality, and Recomputation in Image Processing Pipelines." *Proceedings of the 34th ACM SIGPLAN Conference on Programming Language Design and Implementation (PLDI 2013)*, pages 519-530, 2013. DOI 10.1145/2491956.2462176.
- [Salmon-2011] John K. Salmon, Mark A. Moraes, Ron O. Dror, and David E. Shaw. "Parallel Random Numbers: As Easy as 1, 2, 3." *Proceedings of the 2011 International Conference for High Performance Computing, Networking, Storage and Analysis (SC11)*, article 16, 2011. DOI 10.1145/2063384.2063405.

## 4. Project records

The published editions cite no project record. A file of the public repository is cited by its repository-relative path in the edition that uses it.

## 5. Predecessor engine records

- [spaceFOM-integer] pennyleans. *spaceFOM-integer*. A prototype that publishes the output of an integer spaceflight simulator through the SISO Space Reference FOM (SpaceFOM) over HLA, with a verifier and the measurements behind its results; the simulator source is not included. https://github.com/pennyleans/spaceFOM-integer. Read 2026-10-06.

## 6. Verification log

Status: V, entry verified on 2026-10-02 or on the date in its row; U, unverified.

| Key | Status | How checked | Cited by |
|---|---|---|---|
| Arm-AAPCS64 | V | Document header (date of issue). | SPEC-04P.1 [23] |
| Arm-CMSIS-DSP | V | Quoted phrase "1.15 format" present. | SPEC-01P.1 [12] |
| Blelloch-1990 | V | Report page resolves. | SPEC-00P.1 [15]; SPEC-01P.1 [15] |
| Boucher-2021 | V | arXiv. | SPEC-04P.1 [13] |
| Boute-1992 | V | Crossref. | SPEC-01P.1 [8] |
| Charlesworth-1981 | V | Crossref. | SPEC-00P.1 [20] |
| Clang-LangExt | V | Page resolves. | SPEC-01P.1 [18] |
| Davis-HolyC | V | Archived file: "There is no main() function" and the parentheses rule present. | SPEC-00P.1 [1]; SPEC-04P.1 [5] |
| DLPack | V | GitHub releases API; documentation page titles. | SPEC-00P.1 [10] |
| FIPS-180-4 | V | DOI resolves to the NIST PDF; Crossref title and year. | SPEC-00P.1 [7]; SPEC-01P.1 [22] |
| GCC-Case-Ranges | V | Page resolves. | SPEC-04P.1 [7] |
| GCC-Code-Gen-Options | V | Page resolves. | SPEC-01P.1 [19] |
| GCC-int128 | V | Quoted condition ("integer mode wide enough to hold 128 bits") present. | SPEC-01P.1 [5] |
| GCC-Integer-Overflow-Builtins | V | Page resolves. | SPEC-01P.1 [17] |
| Henriksen-2017 | V | Crossref. | SPEC-00P.1 [3] |
| IEEE-1516-2010 | V | Crossref title; IEEE SA status page. | SPEC-00P.1 [21]; SPEC-04P.1 [39] |
| IEEE-1516.1-2010 | V | Crossref title; IEEE SA status page. | SPEC-04P.1 [40] |
| IEEE-1516.2-2010 | V | Crossref title; IEEE SA status page. | SPEC-04P.1 [41] |
| IEEE-754-2019 | V | Crossref title. | SPEC-01P.1 [14] |
| Intel-SDM | V | Landing page update date (read through a second fetcher); instruction entries not re-read. | SPEC-01P.1 [11] |
| ISO-1539-1-2018 | V | ISO catalog title; J3 draft PDF resolves. | SPEC-00P.1 [5]; SPEC-01P.1 [10]; SPEC-04P.1 [20] |
| ISO-9899-2018 | V | ISO catalog title; N2310 PDF resolves. | SPEC-00P.1 [11]; SPEC-01P.1 [9]; SPEC-04P.1 [1] |
| ISO-9899-2024 | V | ISO catalog title. | SPEC-01P.1 [4] |
| JSON-Lines | V | Statement on `application/jsonl` present. | SPEC-01P.1 [24] |
| Jupyter-Notebook | V | Shift-Enter behavior described on the page. | SPEC-04P.1 [6] |
| Khronos-Vulkan | V | Specification title; memory-model sentence on 8-bit memory locations present. | SPEC-04P.1 [27] |
| Knuth-1997 | V | Standard bibliographic record. | SPEC-00P.1 [12]; SPEC-01P.1 [1]; SPEC-04P.1 [19] |
| Ma-2024 | V | arXiv. | SPEC-00P.1 [17] |
| MISRA-C-2025 | V | Publisher page names MISRA C:2025. | SPEC-04P.1 [31] |
| MITRE-CVE-2021-42574 | V | CVE API record. | SPEC-04P.1 [14] |
| MITRE-CVE-2021-42694 | V | CVE API record. | SPEC-04P.1 [15] |
| MITRE-CWE-484 | V | Page title and version. | SPEC-04P.1 [30] |
| MS-CSharp-Constant-Expressions | V | Source `dotnet/csharpstandard` at `cc01bf4`, `standard/expressions.md`, fetched 2026-10-03: section 12.26 and the sentence "where run-time evaluation would have thrown an exception, compile-time evaluation causes a compile-time error to occur" present. | SPEC-04P.1 [28] |
| MS-CSharp-Integer-Literals | V | Page fetched 2026-10-03 (source `dotnet/csharpstandard` at `cc01bf4`): section 6.4.5.3 and "appears as the token immediately following a unary minus operator token" present. | SPEC-04P.1 [17] |
| MS-CSharp-Lexical-General | V | Page fetched 2026-10-03: section 6.3.1 and "Line terminators, white space, and comments can serve to separate tokens" present. | SPEC-04P.1 [18] |
| MS-CSharp-Named-Args | V | Page resolves. | SPEC-04P.1 [35] |
| MS-x64-Conventions | V | Page resolves. | SPEC-04P.1 [22] |
| NASA-Trick | V | GitHub releases API. | SPEC-00P.1 [23] |
| NASA-TrickHLA | V | GitHub releases API. | SPEC-00P.1 [24] |
| NIST-CAVP-SHA-Vectors | V | Archive downloads. | SPEC-01P.1 [23] |
| NumPy-Interop | V | Page resolves (NumPy 2.5). | SPEC-00P.1 [14] |
| OCallahan-2017 | V | arXiv. | SPEC-00P.1 [8] |
| PEP-3118 | V | Page resolves. | SPEC-00P.1 [9] |
| PEP-515 | V | Page resolves. | SPEC-04P.1 [2] |
| Pharr-2012 | V | Crossref. | SPEC-00P.1 [2]; SPEC-04P.1 [25] |
| PSF-builtins | V | Page fetched 2026-10-03 (Python 3.14.8); "`builtins.open` is the full name for the built-in function `open()`" present. | SPEC-04P.1 [38] |
| PSF-fractions | V | Page resolves. | SPEC-00P.1 [13]; SPEC-01P.1 [27] |
| PSF-Functions-Format | V | Both pages resolve. | SPEC-04P.1 [3] |
| PSF-LangRef | V | Index and expressions chapter resolve; on 2026-10-03, section 8.5 "The with statement", step 7 (`__exit__` invoked when an exception leaves the suite), read. | SPEC-01P.1 [7]; SPEC-04P.1 [4] |
| PSF-stdtypes | V | Quoted "Integers have unlimited precision" present. | SPEC-01P.1 [26] |
| PSF-WhatsNew-3.8 | V | Page resolves. | SPEC-04P.1 [8] |
| pytest-assert | V | Page resolves. | SPEC-04P.1 [9] |
| RaganKelley-2013 | V | Crossref. | SPEC-00P.1 [4]; SPEC-04P.1 [26] |
| RB-Definitions | V | Page resolves. | SPEC-00P.1 [19] |
| RFC-3629 | V | RFC Editor. | SPEC-01P.1 [16]; SPEC-04P.1 [10] |
| RFC-8032 | V | RFC Editor; Crossref (DOI 10.17487/RFC8032). | SPEC-01P.1 [28] |
| RFC-8259 | V | RFC Editor. | SPEC-01P.1 [25]; SPEC-04P.1 [33] |
| Salmon-2011 | V | Crossref. | SPEC-00P.1 [16] |
| SEI-CERT-C-2016 | V | Library page resolves. | SPEC-04P.1 [32] |
| SEI-CERT-INT02-C | U | Redirect to a not-found page. | SPEC-04P.1 [29] |
| SISO-STD-018-2020 | V | PDF text: title page, revision history, approval pages, `HLAinteger64Time`, Rule 4-17, section 5.4.3. | SPEC-00P.1 [22]; SPEC-04P.1 [42] |
| spaceFOM-integer | V | Repository page and README read 2026-10-06: the prototype, the verifier, and the measurements are published there; the simulator source is not. | SPEC-00P.1 [18] |
| Swift-Book | V | File resolves. | SPEC-04P.1 [34] |
| SysV-AMD64-ABI | V | Project page resolves. | SPEC-04P.1 [21] |
| TI-SPRA109 | V | PDF title page. | SPEC-01P.1 [13] |
| UAX-31 | V | Revision and date on the page. | SPEC-04P.1 [37] |
| UAX-44 | V | Revision and date on the page. | SPEC-04P.1 [12] |
| UAX-9 | V | Revision and date on the page. | SPEC-04P.1 [11] |
| Unicode-17.0 | V | Chapter 3 page resolves. | SPEC-04P.1 [16] |
| Unicode-18.0 | V | Version page and ISBN. | SPEC-01P.1 [6] |
| UTS-39 | V | Revision and date on the page. | SPEC-04P.1 [36] |
| W3C-WebGPU | V | Dated URL resolves; `maxStorageBufferBindingSize` present. | SPEC-01P.1 [2] |
| W3C-WGSL | V | Dated URL resolves; quoted note on 64-bit integers, `dot4I8Packed`, and `atomicStoreMin` present. | SPEC-00P.1 [6]; SPEC-01P.1 [3] |
| Wasmtime-Deterministic | V | Page fetched 2026-10-03; "use deterministic fuel-based interruption rather than non-deterministic epoch-based interruption" present. Publisher not stated in the text. | SPEC-01P.1 [21] |
| Wasmtime-Interrupting | V | Page fetched 2026-10-03; the quoted sentence beginning "Fuel-based interruption is completely deterministic" present. Publisher not stated in the text. | SPEC-01P.1 [20] |
| Zig-LangRef | V | `errdefer` present. | SPEC-04P.1 [24] |

## 7. Unverified entries

- [SEI-CERT-INT02-C]: on 2026-10-02 this locator redirected to a page-not-found on the standard's new site, https://cmu-sei.github.io/secure-coding-standards/, whose index still lists INT02-C.

## 8. Maintaining the references

1. A work enters the references in the edition of REFERENCES-P published with the first edition citing it, under a key of the form `Author-Year` for papers and books, the standard or RFC number for standards, and an organization prefix for documentation (for example `MS-`, `GCC-`, `PSF-`).
2. A citing edition copies the entry text word for word into its own list, after its `[n]` number and key, and may add a "Used here" note.
3. How each entry was checked is recorded in section 6. An entry that cannot be checked says "Unverified" with the reason, and the citing edition writes `[citation needed]` where it relies on it.
4. A new edition of a cited work is a new entry with a new key. An entry is never changed in a way that changes what a citing edition relies on.
