# SPEC-01P.3: Canonical encodings and identity

SPEC-01P.3, edition 3 of the canonical encodings specification, cint v0.2.3, 2026-10-23.

This edition replaces SPEC-01P.2. It adds the fixed-point types of 128-bit storage to the tagged values (5.2, 5.4) and the larger bound that their literals give a compile-time fault record (6.3), and it records the calls of externs and the program arguments of `main` in recordings (8.4). The rules of SPEC-01P.1 that define types, operators, faults and fuel are stated in SPEC-04P.3, "The cint language"; this document states the byte encodings and the identities computed from them. Annex C lists every clause identifier of SPEC-01P.1 and where its rule is stated in cint 0.2.2.

<a id="foreword"></a>

## Foreword

This document specifies the bytes that cint 0.2.2 writes and reads: the canonical encodings of values, fault records, program state, checkpoints and recordings and the digests that identify a program and a run.

Each rule ends with one or more clause identifiers in brackets, such as [IM-149] or [IM-157]. An identifier names one rule: a rule keeps its identifier when its text moves to another section or another document, and no identifier is reused. Most identifiers of this document begin with `IM-` and come from SPEC-01P.1. A rule that another edition states, and whose bytes this document lays out, is cited in brackets with that edition's name, such as [SPEC-03P.2 H-14]: the rule is stated there, and this document gives only its encoding. Section numbers belong to this edition only.

<a id="introduction"></a>

## Introduction

A fault has a text form and a machine form. The text form is the fault report; the machine form is the *fault record*: a sequence of bytes, fixed field by field, that every conforming implementation produces identically for the same fault. This program withdraws more than its balance holds, and the conversion of the negative balance to `U8` faults:

```cint ledger.ci
I64 balance = 100;

void withdraw(I64 amount) {
    balance -= amount;
    U8 rest = balance as U8;
    "rest {rest}\n";
}

void main() {
    withdraw(30);
    withdraw(80);
}
```

```text
rest 70
fault[E_NARROW]: the value does not fit the destination type
  --> ledger.ci:5:23
   |
 5 |     U8 rest = balance as U8;
   |                       ^ as.checked.i64.u8
   |
   = operation: as.checked.i64.u8
   = operand-count: 1
   = operand: I64 -10
   = exact:   -10
   = limit:   U8 0
   ...
```

The record of this fault is 151 bytes. It begins with the string `cint-core-1/fault/v2`, preceded by its length, which names the encoding and its version. The code `E_NARROW` follows as the number 6, then the operation identifier, the operand `I64 -10` as a type tag and eight bytes, the exact result, the limit, the position, the revision of the program and the stack. Subclause 6.2 shows every byte.

Tools compare these bytes, not the text of reports. Two implementations agree on a fault when their records are equal, and a recording of a run holds the record and the state hash of every step, so that a replay can compare them again. Every encoding in this document follows the same conventions (5.1): integers are little-endian, every string and list carries its length in front, every optional field has a presence byte, and every binary encoding begins with a domain string that names it. A decoder can therefore read each byte without the program that wrote it, and bytes of two kinds cannot be taken for each other.

Identities are digests of canonical bytes. The *revision* of a program is a SHA-256 digest of its compiled form with every source position removed (9.1), so adding a comment leaves it unchanged. The *state hash* is a SHA-256 digest of the bytes of a program's state (7.6), and a recording chains the state hashes of the steps of a run into one digest (8.5).

<a id="how-this-document-is-organized"></a>

### How this document is organized

Clauses 1 to 4 state what the document covers, the documents it relies on, its terms and what conformance means. Clause 5 encodes values: the conventions every encoding shares, integers of every width, fixed-point values, mathematical integers and tagged values, among them arrays, structs and handles. Clause 6 encodes the fault record, in its versions 2 and 3, and two small records that a compiled program writes beside it. Clause 7 encodes the state of a program, the checkpoint that holds it and the state hash. Clause 8 encodes recordings and cases. Clause 9 defines the identities computed from these bytes and what never enters them. Clause 10 lists future directions. Annex A works through complete encodings, Annex B specifies the operation fixture format CIF-1, and Annex C indexes the clause identifiers of SPEC-01P.1.

Each subclause of clauses 5 to 9 opens with a short summary and then has some of these parts:

- **Syntax**: the layout of the bytes, field by field, in the notation of 5.1.
- **Constraints**: the conditions that valid bytes meet. A writer produces only valid bytes, and a decoder rejects bytes that break a constraint.
- **Semantics**: what the fields mean.
- **Examples** and **Notes**: informative. They illustrate the rules and never add to them.

Byte values are written in hexadecimal, two digits a byte, such as `0f` or `ff`. Every example is the output of cint 0.2.2, of the reference, or of the program shown with it (A.4). A table of bytes lists them in the order they occur, with offsets in decimal. In such a table, `(32 bytes)` stands for a SHA-256 digest, which the text names, and `(n bytes)` for bytes that another table shows. A line `...` in shown output stands for lines left out.

<a id="scope"></a>

## 1 Scope

1. This document specifies the canonical byte encodings of cint 0.2.2, for the language profile `cint-core-1`:
   - values of the types of SPEC-04P.3 6.3 that an encoding holds, and mathematical integers;
   - fault records, and the records of fuel consumed and of an error result that a compiled program writes for the `cint` command;
   - the state of a program, the checkpoint that holds it and the state hash;
   - recordings and cases;
   - the revision identity, the source-map digest and the inputs of execution identity;
   - the operation fixture format CIF-1.
2. This document does not specify:
   - the meaning of values, operators, faults and fuel, which SPEC-04P.3 specifies;
   - the text of fault reports and diagnostics, and the JSON forms that the tools print, which SPEC-04P.3 8.1 and SPEC-06P.2 specify;
   - the canonical SIR text over which the revision identity is computed, the conformance suite and its `.expect` format, which SPEC-09P.2 specifies;
   - the C and Python interfaces through which a host program creates contexts, takes checkpoints and records runs, which SPEC-03P.2 specifies;
   - the files of a saved workbench session, which SPEC-06P.2 specifies.

<a id="normative-references"></a>

## 2 Normative references

The following documents are referred to in the text in such a way that some or all of their content constitutes requirements of this document. For dated references, only the edition cited applies.

- National Institute of Standards and Technology. *Secure Hash Standard (SHS)*. FIPS PUB 180-4, August 2015. Defines SHA-256.
- F. Yergeau. *UTF-8, a transformation format of ISO 10646*. RFC 3629 (STD 63), November 2003.
- T. Bray, editor. *The JavaScript Object Notation (JSON) Data Interchange Format*. RFC 8259 (STD 90), December 2017. Used by CIF-1 (Annex B).
- *JSON Lines*. https://jsonlines.org/. Maintained by Ian Ward. A published convention, with no RFC or ISO standard, for a file of one JSON value per line. Used by CIF-1 (Annex B).

<a id="terms-definitions-and-symbols"></a>

## 3 Terms, definitions and symbols

The terms of SPEC-04P.3 clause 3 apply, among them Z, value, type, exact result, fault, fault code, fault record, operation identifier, error, entry, fuel, call depth, frame arena, semantic input, module, array, kernel, dispatch, work-item, arena, pool, handle, revision, the reference and executor. The following terms are added.

<a id="term-canonical"></a>**3.1 canonical**: designated, by a named and versioned rule, as the one representation of a content or the one selection among several. Canonical says which bytes are compared, not that they are correct: two implementations can write equal canonical bytes and both be wrong.

<a id="term-encoding"></a>**3.2 encoding**: a rule that maps a content, such as a value or a fault record, to bytes. An encoding has a name and a version.

<a id="term-domain-string"></a>**3.3 domain string**: the string, of the form `<profile>/<kind>/v<n>`, that begins each binary encoding of this document and the input of every digest, and names its encoding and version, such as `cint-core-1/fault/v2`.

<a id="term-tag"></a>**3.4 tag**: the bytes, beginning with one byte, that name the type of a value where values of several types can occur. Also called a type descriptor.

<a id="term-payload"></a>**3.5 payload**: the bytes of a value without its tag.

<a id="term-tagged-value"></a>**3.6 tagged value**: a tag followed by a payload.

<a id="term-presence-byte"></a>**3.7 presence byte**: a byte `00`, which marks an optional field as absent, or `01`, which marks it present and is followed by the field.

<a id="term-decoder"></a><a id="term-writer"></a>**3.8 writer, decoder**: a *writer* is the part of an implementation that produces the bytes of an encoding; a *decoder* is the part of an implementation that reads the bytes of an encoding.

<a id="term-context"></a>**3.9 context**: one instance of a program's state, held by a host program, in which entries run one at a time: its module variables, arenas and pools, its buffer registry, its counters and its fault record (SPEC-03P.2 3.4).

<a id="term-state"></a>**3.10 state**: the canonical content of a context between entries: the values of its module-level variables, its arenas and pools, its registry of buffers, its identifier counters, the fault record it holds if any, and the count of effects it has completed.

<a id="term-checkpoint"></a>**3.11 checkpoint**: the canonical bytes of a state, in an envelope that names the runtime contract, from which a context can be restored. A checkpoint is taken between entries, not inside one. A host program receives it as bytes (SPEC-03P.2 9.2), and the tools keep it in a file or, in a workbench session, under a name (SPEC-06P.2 11.1).

<a id="term-state-hash"></a>**3.12 state hash**: the SHA-256 digest of the canonical bytes of a state.

<a id="term-effect"></a>**3.13 effect**: a request that an entry makes of its host. In cint 0.2.2 an effect is the output of a print statement, whose request is the bytes printed, or the call of an extern of the class `observe` or `unrestricted` (SPEC-04P.3 6.9.8), whose request is its arguments.

<a id="term-recording"></a>**3.14 recording**: a file that holds the identity and code of a program, a checkpoint of a context when recording began, and the events that followed, in order: each entry with its arguments, its effects and its outcome, and the checkpoints, restores, cleared faults and changes of revision between entries. The state hash at each boundary is recorded and chained into one digest (3.17). From a recording the run can be replayed: its entries run again on a context restored from that checkpoint, with the recorded inputs, and each outcome and boundary is compared with the recording.

<a id="term-case"></a>**3.15 case**: a recording that also holds the source text of every module of its program, so that the run can be shown, replayed and compared on another machine with no other file.

<a id="term-boundary"></a>**3.16 boundary**: a point of a run at which the state of its context is defined and its state hash is taken: the end of an entry that ran, or, at dispatch density, the publication of a kernel dispatch. A recording records the state hash at each boundary, and replay compares it.

<a id="term-digest-chain"></a>**3.17 digest chain**: the digest that a recording carries in its last event, computed from the state hash of its first checkpoint and of each boundary in turn, so that equal chains mean equal states, and equal fault records, at every boundary.

<a id="term-runtime-contract-version"></a>**3.18 runtime contract version**: the name of the interface between compiled code and the runtime library, `cint-rt-3` in cint 0.2.2.

<a id="term-sir-text"></a>**3.19 SIR text**: the canonical text form of one module's intermediate representation, specified by SPEC-09P.2. Its second line, the *sites line*, lists each *site* of the module, an operation that can fault, with its ordinal, line, column and operation identifier; each node that is a site ends with its ordinal `#n` and its source position.

<a id="term-source-map"></a>**3.20 source map**: the sites of every module of a program, each with its ordinal, line, column and operation identifier. The *source-map digest* is its SHA-256 digest (9.2).

<a id="term-record-kind"></a>**3.21 record kind**: whether a fault record is a *run-time* record, produced by an entry, or a *compile-time* record, produced when a constant expression faults during translation. The kind is not in the bytes; the reader of a record knows it from the place the record came from.

<a id="term-execution-identity"></a>**3.22 execution identity**: the canonical form of the semantic inputs of one entry (9.3).

<a id="conformance"></a>

## 4 Conformance

1. In this document, "shall" states a requirement on an implementation, and "shall not" a prohibition. "May" states a permission.
2. A conforming implementation shall write every encoding that it produces exactly as this document specifies, so that for the same content every conforming implementation writes the same bytes. A structure in memory that holds the same content, such as a C structure of a fault record, is a view of it and is not compared.
3. A conforming decoder shall treat its input as untrusted. It shall reject input that breaks a constraint of this document, it shall check every length and count against the bytes that remain before it allocates memory for what follows, and when it rejects input it shall leave unchanged every state that the input would have changed [IM-148] [SPEC-03P.2 H-19a].
4. Each encoding of clauses 5 to 9 is named by its domain string; the fixture format of Annex B is named CIF-1 and is versioned as B.1 states. An encoding whose layout changes while it carries the same content takes a new version, with its own domain string, and bytes of the earlier version stay readable as that version. A change of the content that an encoding carries for a program, such as a different fault record for the same fault, is a change of the language profile.
5. The bytes of every encoding are the same on every host and every executor. No encoding depends on the host's byte order, word size or alignment or on its clock, locale, file system or environment (9.4).

<a id="values"></a>

## 5 Values

<a id="conventions"></a>

### 5.1 Conventions

Every encoding in this document is built from a few kinds of field, and the same rules of byte order, length and presence apply to all of them.

**Syntax**

| Notation | Bytes |
|---|---|
| `U8`, `U16`, `U32`, `U64` | An unsigned integer of 1, 2, 4 or 8 bytes. |
| `I32`, `I64` | A signed integer of 4 or 8 bytes, in two's complement. |
| `str` | A `U32` length `n`, then `n` bytes. |
| `blob` | A `U64` length `n`, then `n` bytes. |
| `list(x)` | A `U32` count `n`, then `n` fields of the form `x`. |
| `list64(x)` | A `U64` count `n`, then `n` fields of the form `x`. |
| `opt(x)` | A presence byte: `00` alone, or `01` followed by a field of the form `x`. |
| `digest` | 32 bytes, a SHA-256 digest. |
| `"text"` | A `str` whose bytes are the ASCII characters of `text`. |

A layout lists its fields in order, one a line, each with its name, its form and what it holds.

**Constraints**

1. <a id="IM-144"></a>Every integer of more than one byte, in every encoding, is little-endian: its least significant byte comes first. A signed integer is in two's complement at its width [IM-144] [SPEC-03P.2 M-4].
2. <a id="IM-153"></a>Every length and every count is written before the bytes or fields it counts. No encoding uses a terminator, a sentinel value or padding [IM-153].
3. A field that may be absent is preceded by a presence byte. Absence is the byte `00` alone, never a field of zeros or another reserved value. A presence byte other than `00` or `01` is invalid.
4. <a id="IM-152"></a>Each encoding in the table of paragraph 7 begins with its *domain string*: a `str` of lowercase ASCII of the form `<profile>/<kind>/v<n>`, such as `cint-core-1/state/v1`. The last three are the inputs of digests, so the input of every digest of this document begins with one too. The domain string names the encoding and its version, so bytes of two kinds or two versions cannot decode as each other [IM-152].
5. A quantity that a program sees as an `I64`, the canonical word of SPEC-04P.3 6.3.1, is written as an `I64`, whatever representation an implementation uses for it while a program runs: an array's extent, the capacity of an arena or a pool, a handle's length, a dispatch number, a work-item index, a step ordinal, a fuel quantity and a call-depth limit [SPEC-04P.3 IM-7, IM-8]. The lengths and counts that frame the bytes, those of `str`, `blob`, `list` and `list64`, are unsigned, as the table above gives them.
6. A decoder rejects input that ends before a field is complete, and input with bytes left over after the end of what it decodes [IM-148].

**Semantics**

7. The domain strings of cint 0.2.2 are:

| Domain string | Encoding | Section |
|---|---|---|
| `cint-core-1/fault/v2` | Fault record, version 2 | 6.1 |
| `cint-core-1/fault/v3` | Fault record, version 3 | 6.3 |
| `cint-core-1/fuel-consumed/v1` | Fuel consumed by a program's entry | 6.5 |
| `cint-core-1/error-result/v1` | Error result of a program's entry | 6.5 |
| `cint-core-1/state/v1` | State | 7.2 |
| `cint-core-1/arena/v2` | Arena block of a state | 7.3 |
| `cint-core-1/pool/v2` | Pool block of a state | 7.3 |
| `cint-core-1/registry/v1` | Registry block of a state | 7.3 |
| `cint-core-1/checkpoint/v1` | Checkpoint, `CINT-CKPT-1` | 7.5 |
| `cint-core-1/recording/v1` | Recording, `CINT-REC-1` | 8.1 |
| `cint-core-1/case/v1` | Case, `CINT-CASE-1` | 8.6 |
| `cint-core-1/recording-chain/v1` | Input of the digest chain | 8.5 |
| `cint-core-1/revision/v1` | Input of the revision identity | 9.1 |
| `cint-core-1/source-map/v1` | Input of the source-map digest | 9.2 |

8. Each encoding of this table is specified at the version that its domain string names, so the checkpoint, the recording and the case of cint 0.2.2 are specified at version 1. A change of an encoding's layout takes a new version, and bytes of the version specified here stay readable as that version (clause 4, paragraph 4).

**Examples**

9. The `I64` value 1000 is the eight bytes `e8 03 00 00 00 00 00 00`, and the `str` holding the text `fuel-v1` is `07 00 00 00 66 75 65 6c 2d 76 31`.

**Notes**

10. Because every count precedes what it counts, a decoder knows the size of each field before it reads it, and can refuse an impossible length before it allocates memory.

<a id="integer-bool-and-fixed-point-payloads"></a>

### 5.2 Integer, Bool and fixed-point payloads

The payload of a value is its bytes without a type. Every type that an encoding holds has a payload of fixed size, except the mathematical integers of 5.3.

**Syntax**

| Type | Payload |
|---|---|
| `I8`, `I16`, `I32`, `I64` | `w / 8` bytes |
| `I128`, `I256`, `I512`, `I1024` | `w / 8` bytes, in limbs of 8 bytes, limb 0 first |
| `U8`, `U16`, `U32`, `U64` | `w / 8` bytes |
| `U128`, `U256`, `U512` | `w / 8` bytes, in limbs of 8 bytes, limb 0 first |
| `Bool` | 1 byte |
| `Q<i>.<f>` | the payload of its storage integer, of width `i + f` |

**Constraints**

1. <a id="IM-145"></a>A `Bool` payload is `00` for `false` or `01` for `true`; any other byte is invalid [IM-145].

**Semantics**

2. The payload of an integer of width `w` is its `w / 8` bytes, least significant first. A value of a signed type is in two's complement at width `w`, and a value of an unsigned type is its binary representation [IM-145].
3. <a id="IM-12"></a><a id="IM-13"></a>A value of a wide type, `I128` to `I1024` or `U128` to `U512`, is made of `w / 64` limbs. Limb 0 holds bits 0 to 63, limb 1 bits 64 to 127 and the last limb the most significant bits, including the sign bit of a signed type. The payload is the limbs in that order, each limb's 8 bytes least significant first, which makes the whole payload little-endian. There is no sign word, no length and no unused limb: every bit pattern is a value, and every value has one payload [IM-12, IM-13].
4. <a id="IM-14"></a>The payload is the same on every host. At the C interface a wide value is an array of 64-bit limbs in the host's own byte order (SPEC-03P.2); a host whose byte order is big-endian converts each limb when it reads or writes canonical bytes. A type of the C compiler, such as `__int128` or `_BitInt(N)`, never appears in an encoding [IM-14].
5. The payload of a value of the fixed-point type `Q<i>.<f>` is the payload of its raw value, the integer `v * 2^f`, in its storage type `I8`, `I16`, `I32`, `I64` or `I128` of width `i + f`. The payload of a type of 128-bit storage is in two limbs, as an `I128` is. The payload does not say what `f` is; the tag of 5.4 does [IM-145].

**Examples**

6. Payloads of three `I128` values:

| Value | Payload |
|---|---|
| -1 | `ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff` |
| 1 | `01 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00` |
| 2^64 | `00 00 00 00 00 00 00 00 01 00 00 00 00 00 00 00` |

7. The `Q16.16` value 1.5 has the raw value 98304, `0x18000`, so its payload is the `I32` payload `00 80 01 00`. The `Q80.48` value 1.5 has the raw value 1.5 times 2^48, `0x1800000000000`, and its payload is the `I128` payload `00 00 00 00 00 80 01 00 00 00 00 00 00 00 00 00`.

<a id="mathematical-integers"></a>

### 5.3 Mathematical integers

A value in Z, such as the exact result of an operation that overflowed, has no fixed width, so its payload begins with its length.

**Syntax**

```text
z:
  length   U32    n, the number of bytes that follow
  bytes    n bytes, the value in two's complement, least significant first
```

**Constraints**

1. The bytes are the shortest two's complement representation of the value: `n` is the least count of bytes whose two's complement range holds it. The value 0 has `n = 0` and no bytes. A payload is therefore invalid when its one byte is `00`, when its last byte is `00` and the byte before it is below `80`, or when its last byte is `ff` and the byte before it is `80` or above [IM-145, IM-148].
2. The bound on `n` depends on the field and the record that hold the value (6.3).

**Examples**

3. Payloads of values in Z:

| Value | Payload |
|---|---|
| 0 | `00 00 00 00` |
| -1 | `01 00 00 00 ff` |
| 127 | `01 00 00 00 7f` |
| 128 | `02 00 00 00 80 00` |
| -128 | `01 00 00 00 80` |
| 9223372036854775808 | `09 00 00 00 00 00 00 00 00 00 00 80 00` |

4. The value 128 needs a second byte: one byte `80` alone would be -128.
5. Adding 1 to the largest `I128` has the exact result 2^127, which needs 128 bits and a sign bit:

```cint w128.ci
I128 a = I128.max;
"{a}\n";
I128 b = a + 1;
```

```text
170141183460469231731687303715884105727
fault[E_OVERFLOW]: the exact result is outside the type's range
  --> w128.ci:3:12
   |
 3 | I128 b = a + 1;
   |            ^ add.checked.i128
   |
   = operation: add.checked.i128
   = operand-count: 2
   = operand: I128 170141183460469231731687303715884105727
   = operand: I128 1
   = exact:   170141183460469231731687303715884105728
   = limit:   I128 170141183460469231731687303715884105727
   ...
```

6. In the record of this fault, the operands and the exact result are these bytes, from the operand count on; the tags `15` (`I128`) and `0f` (Z) are those of 5.4:

| Offset | Bytes | Field |
|---:|---|---|
| 0 | `02 00 00 00` | operand count, 2 |
| 4 | `15 ff ff ff ff ff ff ff ff ff ff ff ff ff ff ff 7f` | first operand, the largest `I128` |
| 21 | `15 01 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00` | second operand, 1 |
| 38 | `01` | exact: present |
| 39 | `0f 11 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 80 00` | exact: a Z of 17 bytes, 2^127 |

<a id="tagged-values"></a>

### 5.4 Tagged values

Where a field can hold values of several types, each value is written with a tag that names its type, followed by its payload.

**Syntax**

| Tag | Type | Bytes after the tag byte | Payload |
|---|---|---|---|
| `01` | `Bool` | none | 5.2 |
| `0f` | Z | none | 5.3 |
| `11` `12` `13` `14` | `I8` `I16` `I32` `I64` | none | 5.2 |
| `15` `16` `17` `18` | `I128` `I256` `I512` `I1024` | none | 5.2 |
| `21` `22` `23` `24` | `U8` `U16` `U32` `U64` | none | 5.2 |
| `25` `26` `27` | `U128` `U256` `U512` | none | 5.2 |
| `31` | `Q<i>.<f>` | the storage tag `U8`, `11` to `15`; then `f` as `U16` | 5.2 |
| `51` | array | the element tag `U8`; the rank `U8`; each extent `I64` | the elements |
| `71` | struct | the definitions and the type, below | the fields |
| `75` | `Handle` | none | 29 bytes, below |

A struct value, which only a global of a state holds (7.2), has this tag:

```text
struct-tag:
  tag          U8              71
  definitions  list(definition)
  type         U8, U32         71 and the index of the last definition
definition:
  tag          U8              71
  name         str             the struct's qualified name, module.Name, UTF-8
  layout       U8              00
  fields       list(field)
field:
  name         str             the field's name, UTF-8
  bit width    U8              00
  type         field-type
field-type:
  scalar       U8              01, 11 to 18, or 21 to 27
  struct       U8, U32         71 and the index of an earlier definition
  array        U8              52, then field-type, the rank U8 (1 to 4),
                               and for each dimension: 01 as U8, ffffffff as U32, the extent I64
```

In an array field-type, `01` is the shape form of a fixed extent and `ffffffff` stands for no size symbol, as in the type signatures of SPEC-03P.2 7.5.

A handle value, which only a global of a state holds, has this payload:

```text
handle:
  slot         U64
  length       I64
  generation   U64
  container    U32             the arena's or pool's identifier
  kind         U8              00 null, 01 arena, 02 pool
```

**Constraints**

1. <a id="IM-147"></a>A tagged value is its tag followed by its payload [IM-147].
2. <a id="IM-146"></a>In the tag `31` of a fixed-point type, the storage tag is `11`, `12`, `13`, `14` or `15` and gives the storage width `w`, and `f` is at most `w - 1`, so that the type has at least one integer bit [IM-146].
3. <a id="IM-148"></a>In the tag `51` of an array, the element tag is `01`, `11` to `18` or `21` to `27`, the rank is 1 to 8, and each extent is at least 0. The payload is the elements' payloads in row-major order, the last index varying fastest. Its size, the product of the extents and the element size, is at most `I64.max` and equals the bytes that remain for it [IM-146, IM-148].
4. In a struct tag, each definition lists its fields in declaration order. A field's type that is a struct refers to an earlier definition, so a definition never refers to itself or to one after it. The definitions are in the order in which a depth-first walk of the struct's type completes them, each struct once, and the struct of the value is the last, which the type names. Every name is UTF-8 and not empty [IM-146] [SPEC-03P.2 A-21].
5. The payload of a struct value is its fields' payloads in declaration order, with no padding: a nested struct's fields in place, and an array field's elements in row-major order. Its size, computed from the definitions, equals the bytes that remain for it [IM-146, IM-148].
6. In a handle payload the kind is `00`, `01` or `02` and every byte of a handle of kind `00`, the null handle, is `00` [IM-146, IM-148].
7. Each field that holds a tagged value allows only some tags:

| Field | Tags allowed |
|---|---|
| Operand of a fault record (6.1) | `01`, `0f`, `11` to `18`, `21` to `27`, `31` |
| `exact` of a fault record | `0f` |
| `limit` of a fault record | `01`, `0f`, `11` to `18`, `21` to `27`, `31` |
| Value of a global in a state (7.2) | `01`, `11` to `18`, `21` to `27`, `51`, `71`, `75` |
| Argument or result of an entry in a recording (8.4) | `01`, `11` to `14`, `21` to `24` |

   A decoder rejects any other tag in each field [IM-146, IM-148].

**Examples**

8. Tagged values (Annex A.1 has more):

| Value | Tagged bytes |
|---|---|
| `I64` -2 | `14 fe ff ff ff ff ff ff ff` |
| `U8` 255 | `21 ff` |
| `Q16.16` 1.5 | `31 13 10 00 00 80 01 00` |
| Z -1 | `0f 01 00 00 00 ff` |
| `I8[3]` {120, 120, -120} | `51 11 01 03 00 00 00 00 00 00 00 78 78 88` |

9. A module-level struct variable is one global of the state:

```cint sg.ci
struct P { I8 x; Bool b; }
P p = P(x = 5, b = true);

void main() {
    p.x = p.x + 1;
    "{p.x}\n";
}
```

```text
6
```

10. Before `main` runs, the value of the global `sg.p` in the state is these bytes:

| Offset | Bytes | Field |
|---:|---|---|
| 0 | `71` | tag: struct |
| 1 | `01 00 00 00` | 1 definition |
| 5 | `71` | definition 0 |
| 6 | `04 00 00 00` | name, length 4 |
| 10 | `"sg.P"` | name |
| 14 | `00` | layout |
| 15 | `02 00 00 00` | 2 fields |
| 19 | `01 00 00 00` | field name, length 1 |
| 23 | `"x"` | field name |
| 24 | `00` | bit width |
| 25 | `11` | field type `I8` |
| 26 | `01 00 00 00` | field name, length 1 |
| 30 | `"b"` | field name |
| 31 | `00` | bit width |
| 32 | `01` | field type `Bool` |
| 33 | `71 00 00 00 00` | the type: definition 0 |
| 38 | `05 01` | payload: `x` is 5, `b` is `true` |

**Notes**

11. A struct value carries its own type, with the names of its fields, so a decoder reads it without the program. A struct with nested structs and array fields is in Annex A.2.

<a id="decoding-values"></a>

### 5.5 Decoding values

A decoder of any encoding of this document reads tagged values and payloads by the same rules, and rejects bytes that are not a canonical value.

**Constraints**

1. A decoder rejects, before it allocates memory for a payload [IM-148]:
   - a tag that 5.4 does not define, or one that the field does not allow;
   - a `Bool` byte other than `00` and `01`, including a `Bool` element of an array or a struct;
   - a Z whose bytes are not minimal (5.3), or whose length is above the bound of its field and record (6.3);
   - a fixed-point tag whose storage tag is not `11` to `15`, or whose `f` is above `w - 1`;
   - an array of rank 0 or above 8, with a negative extent, or whose payload size is above `I64.max` or differs from the bytes that remain;
   - a handle of a kind above `02`, or a null handle with a byte that is not `00`;
   - a struct tag with a definition that is missing, a name that is empty or not UTF-8, a layout or bit width other than `00`, an array field of rank 0 or above 4 or with another form or size symbol, a reference to its own definition or to one after it, a type that is not the last definition, or a payload whose size differs from the bytes that remain;
   - a length or count larger than the bytes that remain.

<a id="fault-records"></a>

## 6 Fault records

<a id="layout"></a>

### 6.1 Layout

The fault record is the canonical form of a fault. SPEC-04P.3 8.1 says what each field means and how a report shows it; this subclause gives its bytes.

**Syntax**

```text
fault-record:
  domain      str              "cint-core-1/fault/v2", or "cint-core-1/fault/v3" (6.3)
  code        U16              the fault code, 1 to 13
  operation   str              the operation identifier, ASCII
  operands    list(tagged)     the operands, in source order
  exact       opt(tagged)      the exact result, a Z
  limit       opt(tagged)      the bound that was crossed
  position    position         the position of the fault
  revision    opt(digest)      the revision of the program (9.1)
  source-map  opt(digest)      the source-map digest (9.2)
  address     opt(address)     the place in a kernel dispatch
  stack       list(position)   the call sites of the active calls, outermost first
position:
  path        str              the module-relative path, UTF-8
  line        U32              from 1
  column      U32              from 1
address:
  kernel      str              the kernel's qualified name, UTF-8
  dispatch    I64              the dispatch number
  phase       U8               0 entry, 1 work-item, 2 epilogue
  work-item   opt(item)        present in phase 1 only
item:
  index       I64              the work-item index
  step        I64              the step ordinal
```

**Constraints**

1. <a id="IM-149"></a>The code is the number of the record's fault code, from 1 for `E_OVERFLOW` to 13 for `E_ASSERT`, as the table of SPEC-04P.3 5.2.3 gives it [IM-149] [SPEC-04P.3 IM-104].
2. The operation identifier has 1 to 64 bytes, each a lowercase letter, a digit, `.` or `_`. There are at most 8 operands, each a tagged value of a tag that 5.4 allows for an operand [IM-149] [SPEC-04P.3 IM-106].
3. `exact`, when present, is a tagged Z [IM-149].
4. The path of a position has 1 to 1024 bytes of UTF-8, and its line and column are at least 1 [IM-149] [SPEC-04P.3 IM-106].
5. An absent revision, source-map digest or address is a presence byte `00`, never 32 zero bytes or another placeholder [IM-149].
6. The kernel name of an address has at most 256 bytes of UTF-8, and its phase is 0, 1 or 2. The work-item field is present when the phase is 1 and absent otherwise [IM-149].
7. The stack has fewer positions than the entry's call-depth limit, and a decoder rejects a stack of more than 1024 positions [IM-149] [SPEC-04P.3 IM-106].

**Semantics**

8. The bytes of a record are a function of its content alone: the writer takes no padding, alignment or other byte from the structure in memory that holds the record, and conformance compares the bytes, never the structure [IM-149] [SPEC-09P.2 RT-13, RT-14].
9. cint 0.2.2 writes the source-map digest of every fault record as absent. A recording or a case holds the source-map digest of its program in its header (8.2), which names the positions of every record it holds.

**Examples**

10. A fault inside a kernel has an address:

```cint kern.ci
kernel bump[n](in I64[n] old, out I64[n] next) {
    next = old + 1;
}

void main() {
    I64[3] a = [1, 2, 9223372036854775807];
    I64[3] b;
    bump(a, b);
    "{b[0]}\n";
}
```

```text
fault[E_OVERFLOW]: the exact result is outside the type's range
  --> kern.ci:2:16
   |
 2 |     next = old + 1;
   |                ^ add.checked.i64
   |
   = operation: add.checked.i64
   = operand-count: 2
   = operand: I64 9223372036854775807
   = operand: I64 1
   = exact:   9223372036854775808
   = limit:   I64 9223372036854775807
   ...
```

11. The record of this fault ends with these bytes, from the address on:

| Offset | Bytes | Field |
|---:|---|---|
| 0 | `01` | address: present |
| 1 | `09 00 00 00` | kernel, length 9 |
| 5 | `"kern.bump"` | kernel |
| 14 | `00 00 00 00 00 00 00 00` | dispatch number 0 |
| 22 | `01` | phase 1, work-item |
| 23 | `01` | work item: present |
| 24 | `02 00 00 00 00 00 00 00` | work-item index 2 |
| 32 | `02 00 00 00 00 00 00 00` | step ordinal 2 |
| 40 | `01 00 00 00` | stack: 1 position |
| 44 | `07 00 00 00` | path, length 7 |
| 48 | `"kern.ci"` | path |
| 55 | `08 00 00 00` | line number 8 |
| 59 | `05 00 00 00` | column number 5 |

**Notes**

12. No field of a fault record holds a native address, a register, a time, a thread identifier, a backend name or an absolute path, so a fault has the same record on every executor and every host.

<a id="a-worked-record"></a>

### 6.2 A worked record

The record of the fault in the Introduction, byte by byte.

**Examples**

1. `cint run` keeps the bytes of the fault record of a run in the file `fault.bin` of the run's directory, under the directory given by `--cache`:

```console
$ cint run --cache out ledger.ci
rest 70
fault[E_NARROW]: the value does not fit the destination type
...
   = revision: 359672553c501187e1e53e4878d6ede8f1994b52ce0c64c8f3ec4fbdf3f9b3db
...
$ od -An -tx1 -v out/*/run-0/fault.bin
 14 00 00 00 63 69 6e 74 2d 63 6f 72 65 2d 31 2f
 66 61 75 6c 74 2f 76 32 06 00 11 00 00 00 61 73
 2e 63 68 65 63 6b 65 64 2e 69 36 34 2e 75 38 01
 00 00 00 14 f6 ff ff ff ff ff ff ff 01 0f 01 00
 00 00 f6 01 21 00 09 00 00 00 6c 65 64 67 65 72
 2e 63 69 05 00 00 00 17 00 00 00 01 35 96 72 55
 3c 50 11 87 e1 e5 3e 48 78 d6 ed e8 f1 99 4b 52
 ce 0c 64 c8 f3 ec 4f bd f3 f9 b3 db 00 00 01 00
 00 00 09 00 00 00 6c 65 64 67 65 72 2e 63 69 0b
 00 00 00 05 00 00 00
```

2. The revision is the 32 bytes at offsets 92 to 123, the same digest that the report prints. Field by field:

| Offset | Bytes | Field |
|---:|---|---|
| 0 | `14 00 00 00` | domain string, length 20 |
| 4 | `"cint-core-1/fault/v2"` | domain string |
| 24 | `06 00` | code 6, `E_NARROW` |
| 26 | `11 00 00 00` | operation, length 17 |
| 30 | `"as.checked.i64.u8"` | operation |
| 47 | `01 00 00 00` | 1 operand |
| 51 | `14 f6 ff ff ff ff ff ff ff` | operand: `I64` -10 |
| 60 | `01` | exact: present |
| 61 | `0f 01 00 00 00 f6` | exact: Z -10 |
| 67 | `01` | limit: present |
| 68 | `21 00` | limit: `U8` 0 |
| 70 | `09 00 00 00` | position: path, length 9 |
| 74 | `"ledger.ci"` | position: path |
| 83 | `05 00 00 00` | position: line number 5 |
| 87 | `17 00 00 00` | position: column number 23 |
| 91 | `01` | revision: present |
| 92 | (32 bytes) | revision |
| 124 | `00` | source-map digest: absent |
| 125 | `00` | address: absent |
| 126 | `01 00 00 00` | stack: 1 position |
| 130 | `09 00 00 00` | path, length 9 |
| 134 | `"ledger.ci"` | path |
| 143 | `0b 00 00 00` | line number 11 |
| 147 | `05 00 00 00` | column number 5 |

**Notes**

3. The record does not hold the fuel consumed, which the report prints after the record's fields; fuel has its own record (6.5).

<a id="size-bounds-and-version-3"></a>

### 6.3 Size bounds and version 3

Every field of a fault record has a bound, so a decoder can refuse an oversized record before it reads it. The bound on a Z depends on the record's version and kind.

**Constraints**

1. <a id="IM-108"></a>In a run-time record of version 2, every Z, whether an operand, `exact` or `limit`, has at most 257 bytes. The largest such value is the 2048-bit product inside a checked `*` or a `muldiv` of `I1024` operands [IM-108].
2. A compile-time record is of version 2. Its operands may be a Z of up to 513 bytes, which holds the largest integer literal with its sign, and its `exact` a Z of up to 528 bytes, which holds the largest literal scaled to the fixed-point type `Q1.127`: (2^4096 - 1) times 2^127, of 4,223 magnitude bits, with its sign. Its `limit` has at most 257 bytes [IM-108, IM-148].
3. A run-time record whose operation identifier begins with `dot.checked.i1024.` is written in version 3, with the domain string `cint-core-1/fault/v3`. Every other record is written in version 2. Version 3 has the layout of version 2; it differs in its bound alone: every Z of a version 3 record has at most 264 bytes, which holds the exact sum of up to `I64.max` products of two `I1024` values [IM-108, IM-149].
4. No compile-time record is of version 3 [IM-108, IM-149].

**Notes**

5. Each record thus has exactly one encoding: no record that version 2 can hold is written in version 3.

<a id="record-kinds-and-decoding"></a>

### 6.4 Record kinds and decoding

A decoder of a fault record is told the record's kind by its caller, because the same bytes are bounded differently at run time and at compile time.

**Constraints**

1. A decoder is given the record kind with the bytes, which do not carry it: compile-time for a record that a compiler reports with a diagnostic of a faulting constant, run-time for a record of an entry, including every record held in a state, a checkpoint or a recording. A record decoded under the wrong kind is rejected when a field is above that kind's bound [IM-148, IM-149].
2. A decoder rejects, besides the values of 5.5 [IM-148]:
   - a domain string other than the two of 6.1;
   - a code outside 1 to 13, an operation identifier that is empty, longer than 64 bytes or has another character, or more than 8 operands;
   - an `exact` that is not a Z;
   - a run-time record of version 3 whose operation does not begin with `dot.checked.i1024.`, and one of version 2 whose operation does;
   - a version 3 record decoded with the compile-time kind;
   - a position whose path is empty, longer than 1024 bytes or not UTF-8, or whose line or column is 0;
   - an address whose phase is above 2, or with a work item outside phase 1;
   - a stack of more than 1024 positions;
   - bytes after the last position of the stack, when the record is a file of its own.

<a id="records-of-fuel-consumed-and-of-an-error-result"></a>

### 6.5 Records of fuel consumed and of an error result

A compiled program run by the `cint` command reports, beside its fault record, the fuel its entry consumed and an error result when `main` returns an error. Each is a small record with its own domain string.

**Syntax**

```text
fuel-record:
  domain      str     "cint-core-1/fuel-consumed/v1"
  consumed    I64     the fuel consumed by the entry, at least 0
error-record:
  domain      str     "cint-core-1/error-result/v1"
  set         str     the error set's qualified name, module.Set
  value       str     the error value's name
  tag type    U32     the tag of the set's tag type, 21 to 24
  tag         U64     the value's tag, from 1
```

**Constraints**

1. A fuel record has exactly 40 bytes. In an error record the tag is at least 1 and fits the tag type, and nothing follows the tag.

**Semantics**

2. A program writes the fuel record when its entry ran, whether it ended normally, with an error result or with a fault. It writes the error record in place of a fault record when `main` returns an error; `cint run` then exits with status 6 (SPEC-06P.2).

**Examples**

3. A `main` that returns an error:

```console
$ cat err.ci
error LedgerError { overdrawn }

LedgerError!void main() {
    "checking\n";
    return LedgerError.overdrawn;
}
$ cint run --cache out-err err.ci
checking
error: err.LedgerError.overdrawn (tag 1)
$ od -An -tx1 -v out-err/*/run-0/fault.bin
 1b 00 00 00 63 69 6e 74 2d 63 6f 72 65 2d 31 2f
 65 72 72 6f 72 2d 72 65 73 75 6c 74 2f 76 31 0f
 00 00 00 65 72 72 2e 4c 65 64 67 65 72 45 72 72
 6f 72 09 00 00 00 6f 76 65 72 64 72 61 77 6e 22
 00 00 00 01 00 00 00 00 00 00 00
$ od -An -tx1 -v out-err/*/run-0/fuel.bin
 1c 00 00 00 63 69 6e 74 2d 63 6f 72 65 2d 31 2f
 66 75 65 6c 2d 63 6f 6e 73 75 6d 65 64 2f 76 31
 01 00 00 00 00 00 00 00
```

4. The error record names the set `err.LedgerError` and the value `overdrawn`, with the tag type `22`, the tag of `U16` and the tag 1. The fuel record holds the fuel consumed, 1.

<a id="state"></a>

## 7 State

<a id="what-state-holds"></a>

### 7.1 What state holds

The state of a context is everything that one entry leaves for the next. It holds logical values only, so that two executors that agree on a program's meaning write the same state.

**Semantics**

1. SPEC-03P.2 5.1 states what the state of a context holds: its program's revision, profile and fuel model, its module-level variables, its arenas, pools and registered buffers, its identifier counters and count of entries begun, the fault record it holds and its count of effects completed. 7.2 to 7.4 give their bytes [SPEC-03P.2 M-3].
2. <a id="IM-151"></a>State contains logical values only: typed values, declared lengths and logical identifiers. It never contains a native pointer, a device allocation, padding or any other byte that depends on where or how the program runs [IM-151].
3. <a id="IM-155"></a>A quantity of one entry is not state: the fuel allowance, the fuel consumed, the dispatch counter and the call-depth limit are inputs or results of the entry (8.4), and the local variables of active calls are gone between entries [IM-155].
4. <a id="IM-150"></a>7.2 gives the framing of state, its domain strings and its order. The blocks of arenas, pools and the registry (7.3) have their own domain strings, inside that framing [IM-150].

<a id="state-layout"></a>

### 7.2 State layout

**Syntax**

```text
state:
  domain      str              "cint-core-1/state/v1"
  revision    digest           the revision of the program (9.1)
  profile     str              "cint-core-1"
  globals     list64(global)   sorted by the bytes of their names
  arenas      blob             the arena block (7.3), or empty
  pools       blob             the pool block (7.3), or empty
  registry    blob             the registry block (7.3), or empty
  next-buffer U64              the next buffer identifier
  next-arena  U64              the next arena identifier
  next-pool   U64              the next pool identifier
  entries     U64              the entry sequence number
  fuel-model  str              "fuel-v1"
  fault       opt(fault-record)  the fault record held (6.1)
  effects     I64              the effect-log position
global:
  name        str              the qualified name, module.name, UTF-8
  value       tagged           its value (5.4)
```

**Constraints**

1. <a id="IM-156"></a>The globals are sorted by the bytes of their names, each name strictly after the one before it; a name is not empty and is UTF-8 [IM-156].
2. A module-level variable of an integer or `Bool` type is a global whose value has that type's tag. A module-level array is one global whose value is an array (tag `51`) with its elements in row-major order. A module-level struct variable is one global whose value has the struct tag `71`, with its type and its payload. A module-level `Handle` is one global whose value has the handle tag `75` [IM-156].
3. Each of the arena, pool and registry blocks is a `blob`. Its length is 0 when the state has no arena, pool or registered buffer; an arena or pool block stays empty until the first operation on an arena or a pool [IM-156].
4. The fault record is a run-time record, bounded as such (6.3) [IM-156].
5. Each identifier counter is greater than every identifier of its kind in the state. The next buffer identifier is at least 1, and the next arena and pool identifiers are at most 2^32 [IM-156] [SPEC-03P.2 H-19a].
6. The effect-log position is at least 0, and nothing follows it [IM-156] [SPEC-03P.2 H-19a].

**Semantics**

7. The entry sequence number counts the entries begun in the context. The effect-log position counts the effects completed; clearing a held fault changes neither [IM-156].
8. The fault record held in state carries its own domain string, so the version of its encoding is visible inside the state bytes [IM-156].

**Examples**

9. The case that `cint debug locate` saves for `ledger.ci` (8.6) begins with a checkpoint of the state before `main` runs. Its state, from the domain string on, is these 182 bytes:

| Offset | Bytes | Field |
|---:|---|---|
| 0 | `14 00 00 00` | domain string, length 20 |
| 4 | `"cint-core-1/state/v1"` | domain string |
| 24 | (32 bytes) | revision |
| 56 | `0b 00 00 00` | profile, length 11 |
| 60 | `"cint-core-1"` | profile |
| 71 | `01 00 00 00 00 00 00 00` | 1 global |
| 79 | `0e 00 00 00` | name, length 14 |
| 83 | `"ledger.balance"` | name |
| 97 | `14 64 00 00 00 00 00 00 00` | value: `I64` 100 |
| 106 | `00 00 00 00 00 00 00 00` | arena block: empty |
| 114 | `00 00 00 00 00 00 00 00` | pool block: empty |
| 122 | `00 00 00 00 00 00 00 00` | registry block: empty |
| 130 | `01 00 00 00 00 00 00 00` | next buffer identifier 1 |
| 138 | `01 00 00 00 00 00 00 00` | next arena identifier 1 |
| 146 | `01 00 00 00 00 00 00 00` | next pool identifier 1 |
| 154 | `00 00 00 00 00 00 00 00` | entry sequence number 0 |
| 162 | `07 00 00 00` | fuel model, length 7 |
| 166 | `"fuel-v1"` | fuel model |
| 173 | `00` | fault record: absent |
| 174 | `00 00 00 00 00 00 00 00` | effect-log position 0 |

<a id="arena-pool-and-registry-blocks"></a>

### 7.3 Arena, pool and registry blocks

Each block holds one kind of storage of the context and begins with its own domain string.

**Syntax**

```text
arena-block:
  domain      str              "cint-core-1/arena/v2"
  rows        list64(arena)    by identifier, at least 1
  contents    for each module-level arena, in order of its row:
                element type (7.4), then its used elements
arena:
  id          U32              from 1
  parent      U32              the parent's identifier, 0 for a module-level arena
  retired     U8               0 or 1
  offset      I64              the offset in the parent, in elements
  capacity    I64              in elements
  used        I64              the used extent, in elements
  generation  U64
pool-block:
  domain      str              "cint-core-1/pool/v2"
  pools       list64(pool)     at least 1
pool:
  id          U32              its position in the list, from 1
  type        element type (7.4)
  capacity    I64              in slots
  used        U64              the number of slots ever taken
  top         U64              the first slot of the free list, plus 1; 0 when the list is empty
  slots       used times slot
  elements    the payload of each occupied slot's element, in slot order
slot:
  generation  U64
  mark        U8               0 vacant, 1 occupied, 2 retired
  below       U64              the next slot of the free list, plus 1; 0 at its end
registry-block:
  domain      str              "cint-core-1/registry/v1"
  entries     list64(entry)    by identifier, at least 1
entry:
  id          U64
  generation  U64
  origin      U8               1 borrowed, 2 bridge
  element     U16              the element type's tag, or 0
  storage     U8               0
  frac        U16              0
  record      U32              the struct's number, for a struct element; 0 otherwise
  extent      I64
  ceiling     U8               0 read, 1 write: the widest access a view of it may have
  publish     U8               0 none, 1 copy: whether a kernel's output may reach it
  state       U8               0
```

**Constraints**

1. An arena row has 41 bytes. Its identifier is at least 1, its retired byte is 0 or 1, its capacity and used extent are at least 0 with the used extent at most the capacity, and its generation is at most 2^64 - 2 [SPEC-03P.2 H-19a].
2. The rows are in increasing order of identifier. The module-level arenas come first, with identifiers 1 to `n`, not retired and at offset 0. A child arena's parent is an earlier row; the child lies within its parent's capacity, and, unless retired, within its parent's used extent; a child of a retired arena is retired [SPEC-03P.2 H-19a].
3. The element type of each module-level arena is followed by exactly its used elements, and the block ends after the last of them [SPEC-03P.2 M-3, H-19a].
4. Each pool's identifier is its position in the list, from 1, and there are fewer than 2^32 pools. A pool's used slots are at most its capacity. A slot has 17 bytes: a retired slot has the generation 2^64 - 2, a vacant slot a generation of at least 1, every generation is at most 2^64 - 2, and a slot that is not vacant has `below` 0. Starting at `top`, the free list visits every vacant slot exactly once and no other slot. The elements of the occupied slots follow the slots, and the block ends after the last pool [SPEC-03P.2 M-3, H-19a].
5. A registry entry has 37 bytes. The identifiers are at least 1 and increase; a generation is 1 to 2^64 - 2; the origin is 1 or 2; the storage, `frac` and state bytes are 0; `record` is 0 unless the element is a struct; the extent is at least 0; and the element, ceiling and publish fields hold values that SPEC-03P.2 6.1 defines for a registration: the element is 0 only for a borrowed buffer, the ceiling is 0 or 1, and the publish byte is 0 or 1, and 1 only when the ceiling is 1. An entry holds no contents [SPEC-03P.2 M-3, H-19a].

**Semantics**

6. The registry block lists the buffers that a host program has lent to the context (SPEC-03P.2). A context that only runs its program has none.

<a id="element-types"></a>

### 7.4 Element types

An arena or a pool holds elements of one type, which the block writes once, with the size of one element, so that a decoder can step through the elements without the program.

**Syntax**

```text
element-type:
  width        U32              the bytes of one element's payload
  definitions  list(definition) as in a struct tag (5.4)
  type         field-type       a scalar tag, 75, or 71 and the last definition's index
```

**Constraints**

1. With no definitions, the type is a scalar tag or `75`, and the width is the size of that type's payload. With definitions, the type is `71` and the index of the last definition, and the definitions follow the constraints of a struct tag, except that an array field has rank 1 [SPEC-03P.2 M-3, A-21, H-19a].

**Examples**

2. The element type of an arena of `I32` is `04 00 00 00 00 00 00 00 13`: width 4, no definitions, the tag of `I32`. Annex A.3 shows a whole arena block and pool block.

<a id="checkpoints"></a>

### 7.5 Checkpoints

A checkpoint is a state in an envelope. The envelope is not hashed: it names the runtime contract under which the state was taken.

**Syntax**

```text
checkpoint:
  domain       str            "cint-core-1/checkpoint/v1"
  contract     str            the runtime contract version, "cint-rt-3"
  payload      opt(digest)    absent
  build-record opt(digest)    absent
  build        opt(digest)    absent
  state        blob           the state (7.2)
```

**Constraints**

1. The runtime contract version has 1 to 64 bytes, each a printable ASCII character from `21` to `7e`. The length of the state equals the bytes that remain, and the state's bytes are exactly one state [SPEC-03P.2 H-19, H-19a].
2. cint 0.2.2 writes the payload digest, the build record digest and the build identity as absent [SPEC-03P.2 H-15, H-19].

**Semantics**

3. The format is `CINT-CKPT-1`. A checkpoint is the bytes that a host program receives when it checkpoints a context (SPEC-03P.2), and that a workbench session saves for each of its checkpoints (SPEC-06P.2) [SPEC-03P.2 H-19].

**Examples**

4. The checkpoint at the start of the case of `ledger.ci` has 235 bytes: this envelope, then the 182 bytes of state of 7.2.

| Offset | Bytes | Field |
|---:|---|---|
| 0 | `19 00 00 00` | domain string, length 25 |
| 4 | `"cint-core-1/checkpoint/v1"` | domain string |
| 29 | `09 00 00 00` | runtime contract, length 9 |
| 33 | `"cint-rt-3"` | runtime contract |
| 42 | `00` | payload digest: absent |
| 43 | `00` | build record digest: absent |
| 44 | `00` | build identity: absent |
| 45 | `b6 00 00 00 00 00 00 00` | state, length 182 |
| 53 | (182 bytes) | state (7.2) |

<a id="state-hash"></a>

### 7.6 State hash

**Semantics**

1. <a id="IM-154"></a>The state hash of a context is `SHA-256` of its state bytes, from the length of the state's domain string through the effect-log position, as 7.2 lays them out. The envelope of a checkpoint is not part of it [IM-154] [SPEC-03P.2 M-5].
2. An implementation computes SHA-256 itself, and the conformance suite checks it against the digests of fixed inputs; the SHA-256 of the empty input, for example, is `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` [IM-154].
3. State hashes are compared only between states of one runtime contract version. The revision is part of the state, and the revision changes with the runtime contract version (9.1), so rebuilding an unchanged program against another version changes its state hash [IM-156] [SPEC-03P.2 H-15].

**Notes**

4. The fault record held in state is part of the hashed bytes, and its position is part of the record. Moving a faulting line therefore changes the state hash of the faulted state, though the revision stays the same (9.4).

<a id="restoring-a-checkpoint"></a>

### 7.7 Restoring a checkpoint

A restore replaces the state of a context with the state of a checkpoint. Checkpoint bytes are untrusted input, so a restore checks everything before it changes anything.

**Constraints**

1. A restore refuses a checkpoint whose runtime contract version or revision is not exactly the context's; SPEC-03P.2 9.2 states the rule [SPEC-03P.2 H-19].
2. Before it changes any state, a restore checks every constraint of clauses 5 and 7 that the bytes alone can break, and then, against the program, the following; a checkpoint that fails a check is refused, and the context is left exactly as it was [SPEC-03P.2 H-19a]:
   - the globals are exactly the program's module-level variables, each with the same qualified name and the same type;
   - the module-level arenas and pools are exactly those that the program declares, each with the same element type;
   - every struct number of the registry names a struct of the program;
   - a held fault record has a position that names a site of the program.

**Semantics**

3. A restore sets each identifier counter as SPEC-03P.2 6.1 states, so that no identifier is issued twice in a context's lifetime; a recording records the counters it chose (8.4) [SPEC-03P.2 M-11].

<a id="recordings"></a>

## 8 Recordings

<a id="the-recording-file"></a>

### 8.1 The recording file

A recording holds the program's identity and code, a checkpoint of the context when recording began, and the events that followed. With them, the entries run in a context can be replayed and each step compared with what happened.

**Syntax**

```text
recording:
  header       header (8.2)     the domain string "cint-core-1/recording/v1", or "cint-core-1/case/v1"
  code         code (8.3)
  initial      blob             a checkpoint (7.5) of the context when recording began
  events       event (8.4)      one or more, the end event last
```

**Constraints**

1. The initial checkpoint is a valid checkpoint whose revision is the revision of the header. The last event is the end event, and no byte follows it [SPEC-03P.2 H-14].

**Semantics**

2. The format is `CINT-REC-1`. A host program makes a recording through a recorder (SPEC-03P.2); `cint workbench` records every session; `cint debug locate` saves a case (8.6) [SPEC-03P.2 H-14].

<a id="header"></a>

### 8.2 Header

**Syntax**

```text
header:
  domain       str              "cint-core-1/recording/v1", or "cint-core-1/case/v1"
  profile      str              "cint-core-1"
  fuel-model   str              "fuel-v1"
  contract     str              the runtime contract version
  revision     digest           the revision of the code (9.1)
  source-map   opt(digest)      the source-map digest of the code (9.2)
  mode         U8               0 full, 1 digest
  interval     U64              1
  density      U8               0 entry, 1 dispatch
```

**Constraints**

1. <a id="H-14a"></a>The runtime contract version has 1 to 64 bytes from `21` to `7e`. The mode is 0 or 1, the interval is 1, and the density is 0 or 1 [H-14a] [SPEC-03P.2 H-14].
2. The source-map digest is present exactly when the code gives one (9.2), and it equals the digest of the code [SPEC-03P.2 H-14].

**Semantics**

3. The interval 1 means that the state hash of every entry is recorded. At entry density the boundaries are the ends of entries; at dispatch density each dispatch of a kernel is a boundary too [H-14a] [SPEC-03P.2 H-14].
4. A recording in digest mode differs from one in full mode in its mode byte and in the events of extern calls, which hold the SHA-256 of the elements of each view where full mode holds the elements (8.4).

**Examples**

5. The header of the case of `ledger.ci`, through the module count of its code:

| Offset | Bytes | Field |
|---:|---|---|
| 0 | `13 00 00 00` | domain string, length 19 |
| 4 | `"cint-core-1/case/v1"` | domain string |
| 23 | `0b 00 00 00` | profile, length 11 |
| 27 | `"cint-core-1"` | profile |
| 38 | `07 00 00 00` | fuel model, length 7 |
| 42 | `"fuel-v1"` | fuel model |
| 49 | `09 00 00 00` | runtime contract, length 9 |
| 53 | `"cint-rt-3"` | runtime contract |
| 62 | (32 bytes) | revision |
| 94 | `01` | source-map digest: present |
| 95 | (32 bytes) | source-map digest |
| 127 | `00` | mode: full |
| 128 | `01 00 00 00 00 00 00 00` | interval 1 |
| 136 | `00` | density: entry |
| 137 | `01 00 00 00` | code: 1 module |

<a id="code"></a>

### 8.3 Code

A recording carries the code it ran, so a replay needs no other file and reproduces every fault record byte for byte.

**Syntax**

```text
code:
  modules      list(module)
module:
  path         str              the module-relative path, ending in ".ci"
  sir          blob             the module's SIR text, with its sites line
  source       opt(blob)        the module's source text
```

**Constraints**

1. Each path has 4 to 1024 bytes of UTF-8 and ends in `.ci` [SPEC-03P.2 H-14].

**Semantics**

2. The code holds every module of the program: the root module and every module it imports. Each module's SIR text is the one from which the revision (9.1) and the source-map digest (9.2) are computed. A module's source text is present when the host supplied it, and in every module of a case [SPEC-03P.2 H-14].

<a id="events"></a>

### 8.4 Events

Each event is one byte that gives its kind, followed by its fields.

**Syntax**

```text
event 1, entry:
  sequence     U64              the entry sequence number, from 1
  form         U8               0 an exported function, 1 the program entry
  module       str              the path of the function's module; empty for the program entry
  function     str              the function's name; empty for the program entry
  arguments    list(tagged)     at most 16; none for the program entry
  fuel         I64              the fuel budget, or -1 for no budget
  depth        I64              the call-depth limit, at least 1
  frame-arena  I64              the frame-arena capacity, at least 0
  inputs       U32              0, or 1 for program arguments
  arguments    input            for inputs 1 only: the program arguments
event 2, effect:
  ordinal      I64              the effect-log position of the effect, from 0
  site         U32, U32         the module's number in the program and the site's ordinal
  name         str              "print", or the name of the extern called
  status       I32              the status that the host returned
  request      blob             the bytes printed, or the request block of the call
  response     blob             for the call of an extern only: the response block
event 3, outcome:
  status       I32              0 to 5
  fault        opt(str)         the fault record, as a length and its bytes
  consumed     I64              the fuel consumed, 0 when the entry did not run
  result       opt(tagged)      the value returned
  error        opt(error)       the error returned
  outputs      U32              0
  state-hash   opt(digest)      the state hash after the entry
  allowance    I64              the fuel allowance at the end, or -1 for none
error:
  set          str              the error set's qualified name
  tag          U64              the error value's tag, from 1
event 4, clear:
  record       digest           SHA-256 of the fault record cleared
event 5, checkpoint:
  state-hash   digest           the state hash of the checkpoint taken
event 6, restore:
  state-hash   digest           the state hash of the checkpoint restored
  next-buffer  U64              the identifier counters after the restore
  next-arena   U64
  next-pool    U64
  checkpoint   blob             the checkpoint restored (7.5)
event 7, revision:
  revision     digest           the new revision
  source-map   opt(digest)      as in the header
  code         code             the code of the new revision
event 8, end:
  reason       U8               0 closed by the host, 1 a resource failure, 2 an unrestricted call
  chain        digest           the head of the digest chain (8.5)
  count        U64              the number of events before it
event 9, dispatch:
  number       I64              the dispatch number, at least 0
  state-hash   digest           the state hash after the dispatch published
event 10, unrestricted:
  ordinal      I64              the effect-log position of the call, from 0
  site         U32, U32         the module's number in the program and the site's ordinal
  name         str              the name of the extern called
request block:
  count        U32              the number of arguments
  arguments    argument         one for each argument, in declaration order
argument, a scalar:
  value        tagged           the argument's tagged value (5.4)
argument, a view:
  kind         U8               52
  element      U8               the tag of the element type
  permission   U8               0 in, 1 out
  extent       U64              the number of elements
  elements     elements         for an in view only: its elements
response block, when the status is 0:
  alternative  U8               0 the value, 1 an error
  tag          U64              for an error: the error value's tag, from 1
  result       tagged           for a value: the result, absent when it is void
  outputs      elements         for a value: the elements of each out view, in declaration order
input, the program arguments:
  kind         U8               52
  element      U8               the tag of U8
  permission   U8               0
  extent       U64              the number of bytes, at least 1
  length       U64              the extent in full mode, 32 in digest mode
  bytes        length bytes     the arguments in full mode; their SHA-256 in digest mode
elements, in full mode:
  each element in index order, its payload (5.2)
elements, in digest mode:
  SHA-256 of the payloads of the elements in index order, 32 bytes
```

**Constraints**

1. Events come in this order: an entry, then its effects and dispatch events in the order they happened, then its outcome. Effect, dispatch and outcome events occur only inside an entry, and the other events only outside one. A dispatch event occurs only at dispatch density [SPEC-03P.2 H-14].
2. In an entry event, the program entry has an empty module, an empty function and no arguments. Arguments are values of `Bool`, `I8` to `I64` or `U8` to `U64`. Only the program entry has an input, when its context holds program arguments (SPEC-03P.2 7.2); in full mode their last byte is 0 [SPEC-03P.2 H-14].
3. The statuses of an outcome are 0 `CINT_OK` and 1 `CINT_FAULT`, for an entry that ran, and 2 `CINT_REFUSED`, 3 `CINT_BUSY`, 4 `CINT_FAULTED` and 5 `CINT_RESOURCE`, for one that did not. An outcome fits its entry [SPEC-03P.2 H-14]:
   - for an entry that ran, the state hash is present; the fault record is present exactly when the status is 1; a result and an error are never both present, and neither is present with status 1; the allowance is the entry's fuel budget, or a lower budget of at least 0 that an interrupt set; and the fuel consumed is at most the allowance when the allowance is not -1;
   - for an entry that did not run, the state hash, the result and the error are absent, the fuel consumed is 0, the allowance is the entry's fuel budget, and a fault record is present only with status 5.
4. After an outcome with status 5 the next event is the end, with reason 1, and an end with reason 1 occurs only there [SPEC-03P.2 H-14].
5. A restore event's state hash is the state hash of its checkpoint, and each counter it records is at least the checkpoint's [SPEC-03P.2 H-14, M-11].
6. The count of the end event equals the number of events before it [SPEC-03P.2 H-14].
7. An effect whose name is `print` has no response. Any other effect is the call of an extern: its request holds at least the count, and its response is empty unless the status is 0. The name of a call of a standard service is the service's name, its version and the operation, such as `fs/1.stat`, and that of any other extern is its declared name [SPEC-03P.2 H-14].
8. Arguments and results are values of `Bool`, `I8` to `I64` and `U8` to `U64`, and a view's elements are of those types; a `Grants` argument is the `I64` 0 [SPEC-03P.2 A-15].
9. An unrestricted event occurs only inside an entry, and the next event is the end, with reason 2. The entry has no outcome. An end with reason 2 occurs only there [SPEC-03P.2 H-14].

**Semantics**

10. The request of a print's effect is the bytes that its print statement wrote. The concatenation of the requests of an entry's print effects is what the entry printed, including the bytes printed before a fault [SPEC-03P.2 H-14].
11. The call of an `observe` extern is recorded as an effect, after the host returned: the request block holds what the call passed, and the response block what the host returned in the result and in each `out` view. A call of a `pure` extern is not recorded, because it depends on its arguments only. A call that the host refused, with a status other than 0, has an empty response, and the entry faults (SPEC-03P.2 A-15) [SPEC-03P.2 H-14].
12. The call of an `unrestricted` extern is recorded as an unrestricted event, before the call, and ends the recording: the rest of the entry runs and is not recorded, and a replay stops before that event (SPEC-03P.2 H-17) [SPEC-03P.2 H-14].
13. A revision event records a change of the context's program to another revision between entries, with that revision's code, so that a replay applies it [SPEC-03P.2 H-14, H-19b].

**Examples**

14. The events of the case of `ledger.ci`: the program entry, its one print and its outcome, a fault; then the end. The 151 bytes of the fault record are those of 6.2.

| Offset | Bytes | Field |
|---:|---|---|
| 0 | `01` | event: entry |
| 1 | `01 00 00 00 00 00 00 00` | sequence 1 |
| 9 | `01` | form: the program entry |
| 10 | `00 00 00 00` | module: empty |
| 14 | `00 00 00 00` | function: empty |
| 18 | `00 00 00 00` | no arguments |
| 22 | `ff ff ff ff ff ff ff ff` | fuel budget: none |
| 30 | `00 01 00 00 00 00 00 00` | call-depth limit 256 |
| 38 | `00 00 00 01 00 00 00 00` | frame-arena capacity 16777216 |
| 46 | `00 00 00 00` | inputs 0 |
| 50 | `02` | event: effect |
| 51 | `00 00 00 00 00 00 00 00` | ordinal 0 |
| 59 | `00 00 00 00` | site: module 0 |
| 63 | `03 00 00 00` | site: ordinal 3 |
| 67 | `05 00 00 00` | name, length 5 |
| 71 | `"print"` | name |
| 76 | `00 00 00 00` | status 0 |
| 80 | `08 00 00 00 00 00 00 00` | request, length 8 |
| 88 | `72 65 73 74 20 37 30 0a` | request: `rest 70` and a line feed |
| 96 | `03` | event: outcome |
| 97 | `01 00 00 00` | status 1, `CINT_FAULT` |
| 101 | `01` | fault record: present |
| 102 | `97 00 00 00` | fault record, length 151 |
| 106 | (151 bytes) | fault record (6.2) |
| 257 | `03 00 00 00 00 00 00 00` | fuel consumed 3 |
| 265 | `00` | result: absent |
| 266 | `00` | error: absent |
| 267 | `00 00 00 00` | outputs 0 |
| 271 | `01` | state hash: present |
| 272 | (32 bytes) | state hash after the entry |
| 304 | `ff ff ff ff ff ff ff ff` | allowance: none |
| 312 | `08` | event: end |
| 313 | `00` | reason: closed by the host |
| 314 | (32 bytes) | head of the digest chain |
| 346 | `03 00 00 00 00 00 00 00` | 3 events before it |

15. The first effect of a recording, in full mode, of the program `denied_fallback.ci` of SPEC-04P.3 6.9.9, run with no grant: the call of `fs.stat` at site 1 of the program's second module, which returns the error `denied`, the second value of `FsError`. Its `Grants` argument is the `I64` 0, and its path is an `in` view of 18 `U8` elements.

| Offset | Bytes | Field |
|---:|---|---|
| 0 | `02` | event: effect |
| 1 | `00 00 00 00 00 00 00 00` | ordinal 0 |
| 9 | `01 00 00 00` | site: module 1 |
| 13 | `01 00 00 00` | site: ordinal 1 |
| 17 | `09 00 00 00` | name, length 9 |
| 21 | `"fs/1.stat"` | name |
| 30 | `00 00 00 00` | status 0 |
| 34 | `2a 00 00 00 00 00 00 00` | request, length 42 |
| 42 | `02 00 00 00` | request: 2 arguments |
| 46 | `14 00 00 00 00 00 00 00 00` | request: the `I64` 0 |
| 55 | `52 21 00` | request: a view of `U8`, in |
| 58 | `12 00 00 00 00 00 00 00` | request: extent 18 |
| 66 | `"/data/greeting.txt"` | request: the 18 elements |
| 84 | `09 00 00 00 00 00 00 00` | response, length 9 |
| 92 | `01` | response: an error |
| 93 | `02 00 00 00 00 00 00 00` | response: tag 2, `denied` |

<a id="digest-chain"></a>

### 8.5 Digest chain

The digest chain folds the state hash of every boundary into one digest, so that two recordings with equal chains passed through equal states with equal fault records.

**Syntax**

```text
D_0 = SHA-256( "cint-core-1/recording-chain/v1", the state hash of the initial checkpoint )
D_k = SHA-256( "cint-core-1/recording-chain/v1", D_(k-1), boundary_k )
boundary:
  entry        U64              the index of the boundary's entry event, counting events from 0
  kind         U8               0 the end of an entry, 1 a dispatch
  dispatch     opt(I64)         the dispatch number
  state-hash   digest           the state hash at the boundary
  fault        opt(str)         the fault record, as a length and its bytes
```

The domain string enters each digest as a `str`, its length then its bytes.

**Constraints**

1. Each entry that ran, with status 0 or 1, adds one boundary of kind 0 at its outcome, with the outcome's state hash and fault record. An entry that did not run adds none.
2. A dispatch event adds a boundary of kind 1 with its number and state hash and no fault record, unless the outcome of its entry follows it directly with the same state hash. That outcome's boundary of kind 0 then carries the dispatch number, and the dispatch adds no boundary of its own.
3. The head of the chain is the last `D_k`, and the end event holds it.

**Notes**

4. The chain detects a difference at a boundary. A difference that appears and disappears between two boundaries leaves the chain unchanged. Annex A.4 recomputes the chain of the case of `ledger.ci`.

<a id="cases"></a>

### 8.6 Cases

A case is a recording that carries its program's source text, so that the debug tools can show the source of every position in it.

**Constraints**

1. A case has the domain string `cint-core-1/case/v1` and is in full mode, and every module of its code, and of the code of each revision event, has its source text. A decoder refuses a case without one, or in digest mode [SPEC-03P.2 H-14b].

**Semantics**

2. The format is `CINT-CASE-1`. Apart from its domain string, a case is laid out as a recording [SPEC-03P.2 H-14b].

**Examples**

3. `cint debug locate` runs a program on the interpreter and saves the run as a case. `cint debug replay` decodes the case, replays it, compares each boundary with the recording and prints the case's revision, its source-map digest, the head of its digest chain and the final state hash:

```console
$ cint debug locate ledger.ci --save-case ledger.case --out dbg
program    ledger.ci   evaluator cpu-sir-interp, recorded as the case ledger.case
target     boundary 1 of 1, the program entry: the first fault, reproduced by replay
...
$ cint debug replay ledger.case
case       ledger.case   1 boundary   3 events   mode full   density entry
revision   359672553c501187e1e53e4878d6ede8f1994b52ce0c64c8f3ec4fbdf3f9b3db
source-map 8c97a22e8332385f6b94e1782030208003280b8e6859b1775f2a93caa402e87e
boundary  event  entry                          outcome                  fuel
       1      2  the program entry              faulted E_NARROW         3
replay     ok, 1 of 1 boundary compared with the recording
chain      e54344d0fb4bcc2af778845380e8b60127eb639189fad8b9df78e45b60c34795
state      6699607c6fabf35dc4931ef60208e1fdf6cde9838caf325470be34ce0807a279
```

4. The revision is the one in the report of 6.2, recomputed by the interpreter from the SIR text in the case. The chain line is the head of the digest chain, and the state line the state hash after the entry, both as the end and outcome events of 8.4 hold them.

<a id="decoding-a-recording"></a>

### 8.7 Decoding a recording

**Constraints**

1. A decoder checks a whole recording before any part of it is used, and refuses it entirely when a check fails [SPEC-03P.2 H-14, H-17a]:
   - the header (8.2), the code (8.3), the initial checkpoint (7.5) and every checkpoint of a restore event;
   - that the revision of the initial checkpoint is the revision of the header;
   - the source-map digest of the header and of each revision event against its code;
   - the order and fields of every event (8.4), the request and response blocks of each extern call, and each outcome against its entry;
   - the digest chain and the event count against the end event;
   - that the file ends with the end event: a file without one is incomplete and is never loaded in part.

<a id="identity"></a>

## 9 Identity

<a id="revision-identity"></a>

### 9.1 Revision identity

The revision identity of a program names its meaning under one runtime contract. It is computed from the program's SIR text with every source position removed, so it changes when the program's operations change and not when only their positions do.

**Syntax**

```text
revision = SHA-256(
  "cint-core-1/revision/v1"      str
  "cint-core-1"                  str, the profile
  contract                       str, the runtime contract version
  modules                        U32, the number of modules
  for each module, in the byte order of its path:
    path                         str, the module-relative path, UTF-8
    text                         blob, the module's position-free SIR text
)
```

**Semantics**

1. The *position-free SIR text* of a module is its SIR text with its `sites` line and every position annotation removed, and its site ordinals kept; SPEC-09P.2 6.5 states the rule [SPEC-09P.2 SIR-17].
2. <a id="IM-157"></a>The revision of a program is the digest above, over the root module and every module it imports. It is computed over the program's meaning, not over its source formatting, and not over an executable or a kernel binary [IM-157] [SPEC-09P.2 SIR-17].
3. This one value is the revision of fault records (6.1), of state (7.2), of checkpoints and of recordings and cases (8.2). There is no second digest of a program's code [IM-157] [SPEC-03P.2 H-15].
4. The runtime contract version is an input: rebuilding an unchanged program against another runtime contract version changes its revision, and so the state hash of each of its states [IM-157] [SPEC-03P.2 H-15].
5. The fuel model is not an input of the revision; it is a separate input of execution identity (9.3) [SPEC-09P.2 SIR-17].

**Notes**

6. Adding a comment, a blank line or a space or moving a function, changes positions and leaves the revision unchanged. Reordering two statements, or adding an operation that can fault, changes the site ordinals and therefore the revision.

<a id="source-map-digest"></a>

### 9.2 Source-map digest

The source-map digest records the positions that the revision leaves out: with it, a site ordinal in a recording can be traced to a line and column.

**Semantics**

1. The source-map digest of a program is a SHA-256 digest over the `sites` line of each module's SIR text, which lists every site's ordinal, line, column and operation; SPEC-09P.2 6.5 states the rule and its bytes [SPEC-09P.2 SIR-17].
2. Every executor computes the source-map digest from the same SIR text, so every executor gets the same value. A recording holds it in its header and in each revision event (8.2) [SPEC-09P.2 SIR-17].

<a id="execution-identity"></a>

### 9.3 Execution identity

The execution identity of an entry is the canonical form of everything that decides its outcome.

**Semantics**

1. The execution identity of an entry is the tuple of its semantic inputs, from the profile and the fuel model to its frame-arena capacity; SPEC-04P.3 8.2 states the rule [SPEC-04P.3 IM-159].
2. A recording holds each of these in canonical form: the header holds the profile, the fuel model, the revision and the source-map digest; the entry event holds the entry, its arguments as tagged values and its three limits; the state hash before the entry is the state hash of the boundary or checkpoint before it; and each effect event holds the status that the host supplied for the effect [SPEC-04P.3 IM-159] [SPEC-03P.2 H-14].
3. SPEC-04P.3 8.2 states what two entries with equal execution identity agree on: their output, their fault record, their fuel consumed and the state they leave.

<a id="what-never-enters-an-identity"></a>

### 9.4 What never enters an identity

**Semantics**

1. The revision depends only on the profile, the runtime contract version and the position-free SIR text of each module with its module-relative path. The directory a tool runs in, the absolute path of a file, the time, the time zone, the locale, the environment, the executor, the C compiler and the host do not enter it [IM-157] [SPEC-03P.2 H-15].
2. The build identity, the payload digest and the build record digest are separate fields of the checkpoint envelope; they enter neither the revision nor the state hash. Executables of one program built for different executors or machines, under one runtime contract version, have one revision [SPEC-03P.2 H-15].
3. State holds no time, locale, path outside a module-relative path, native address, thread identifier or backend name, so neither does a state hash [IM-151].

**Examples**

4. A comment line moves every position of `ledger.ci` down one line. `moved/ledger.ci` is that program, and a run from another directory, with another time zone and locale, sees the same program:

```console
$ cat moved/ledger.ci
// A ledger with one account.
I64 balance = 100;

void withdraw(I64 amount) {
    balance -= amount;
    U8 rest = balance as U8;
    "rest {rest}\n";
}

void main() {
    withdraw(30);
    withdraw(80);
}
$ cint run moved/ledger.ci
rest 70
fault[E_NARROW]: the value does not fit the destination type
  --> ledger.ci:6:23
...
   = revision: 359672553c501187e1e53e4878d6ede8f1994b52ce0c64c8f3ec4fbdf3f9b3db
...
$ cd moved && TZ=Asia/Tokyo LC_ALL=C cint run ../ledger.ci
rest 70
fault[E_NARROW]: the value does not fit the destination type
  --> ledger.ci:5:23
...
   = revision: 359672553c501187e1e53e4878d6ede8f1994b52ce0c64c8f3ec4fbdf3f9b3db
...
```

5. All three runs have the revision of 6.2. The source map of the moved program differs, and so does the state hash after its fault, whose record holds line 6:

```console
$ cint debug locate moved/ledger.ci --save-case moved.case --out dbg-moved
program    moved/ledger.ci   evaluator cpu-sir-interp, recorded as the case moved.case
...
$ cint debug replay moved.case
case       moved.case   1 boundary   3 events   mode full   density entry
revision   359672553c501187e1e53e4878d6ede8f1994b52ce0c64c8f3ec4fbdf3f9b3db
source-map 9468edf527b220f336c50ae1bc85e775c1d5de45f9665d014a75f270735f8360
boundary  event  entry                          outcome                  fuel
       1      2  the program entry              faulted E_NARROW         3
replay     ok, 1 of 1 boundary compared with the recording
chain      e0fb9977a7d7e4e0fe9b16a25442582caa4931e0db53cf2330f7b30e07dfbd2f
state      3db1185b4fd68a6cf49fa7d99016e839dd3d9fe53076d3c59660def4db41c2ce
```

**Notes**

6. A fault record holds module-relative paths, so the directory from which a program runs does not change it.

<a id="future-directions"></a>

## 10 Future directions

This clause is informative. It lists encodings that the design of cint names and that cint 0.2.2 does not write; none of them is a rule of this edition.

1. **Arguments and supplied effects as digests.** An entry's arguments, with the full contents of any array a host lends, encoded under `cint-core-1/args/v1` and hashed, and a digest of what the host supplied to the entry, so that an execution identity is a short tuple of digests.
2. **View identities in fault records.** A tag for the identity of a view, with its buffer, generation, shape, strides and permission and none of its contents, so that the record of a reduction can name the view it read.
3. **Fixed-point globals.** A module-level variable of a fixed-point type as a global of state, with the tag `31`.
4. **Build digests.** The payload digest, the build record digest and the build identity of a checkpoint's envelope and of a recording, written and checked.
5. **Fuller recordings.** Digest mode that records digests of the contents of lent buffers instead of the contents; input and output records; registration and release events; a state-hash interval above 1.
6. **A tree state hash.** A state hash built from digests of fixed-size chunks of each buffer, so that a device can hash its own memory and transfer only digests.
7. **Migrations.** A checkpoint restored into another revision of its program through a migration that the program declares, recorded as an event.
8. **`U1024`.** An unsigned 1024-bit type, with its own tag.

<a id="worked-encodings"></a>

## Annex A (informative) Worked encodings

<a id="tagged-values-2"></a>

### A.1 Tagged values

| Value | Tagged bytes |
|---|---|
| `Bool` `true` | `01 01` |
| `I8` -1 | `11 ff` |
| `I16` 1000 | `12 e8 03` |
| `I32` -2 | `13 fe ff ff ff` |
| `I64` -2 | `14 fe ff ff ff ff ff ff ff` |
| `I64` 1000 | `14 e8 03 00 00 00 00 00 00` |
| `U8` 255 | `21 ff` |
| `U16` 65535 | `22 ff ff` |
| `I128` 2^64 | `15 00 00 00 00 00 00 00 00 01 00 00 00 00 00 00 00` |
| `U128` 1 | `25 01 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00` |
| `Q16.16` 1.5 | `31 13 10 00 00 80 01 00` |
| `Q8.8` -0.5 | `31 12 08 00 80 ff` |
| `Q80.48` 1.5 | `31 15 30 00 00 00 00 00 00 80 01 00 00 00 00 00 00 00 00 00` |
| Z 0 | `0f 00 00 00 00` |
| Z -1 | `0f 01 00 00 00 ff` |
| Z 9223372036854775808 | `0f 09 00 00 00 00 00 00 00 00 00 00 80 00` |
| `I8[3]` {120, 120, -120} | `51 11 01 03 00 00 00 00 00 00 00 78 78 88` |
| `U8[2, 2]` {{1, 2}, {3, 4}} | `51 21 02 02 00 00 00 00 00 00 00 02 00 00 00 00 00 00 00 01 02 03 04` |

<a id="a-struct-with-nested-structs-and-an-array-field"></a>

### A.2 A struct with nested structs and an array field

1. This program has module-level variables of five kinds:

```cint st.ci
struct Pt { I32 x; I32 y; }
struct Seg { Pt a; Pt b; I8[2] tag; }
I64 total = 3;
Bool ready = true;
I128 big = 18446744073709551616;
U8[2, 2] grid = [[1, 2], [3, 4]];
Seg s = Seg(a = Pt(x = 1, y = 2), b = Pt(x = 3, y = 4), tag = [7, 8]);

void main() {
    total += 1;
    "{total}\n";
}
```

```text
4
```

2. In the state before `main` runs, the globals are in the byte order of their names: `st.big`, `st.grid`, `st.ready`, `st.s`, `st.total`. The value of `st.s` lists `Pt`, which `Seg` uses, before `Seg`:

| Offset | Bytes | Field |
|---:|---|---|
| 0 | `71` | tag: struct |
| 1 | `02 00 00 00` | 2 definitions |
| 5 | `71` | definition 0 |
| 6 | `05 00 00 00` | name, length 5 |
| 10 | `"st.Pt"` | name |
| 15 | `00` | layout |
| 16 | `02 00 00 00` | 2 fields |
| 20 | `01 00 00 00` | field name, length 1 |
| 24 | `"x"` | field name |
| 25 | `00` | bit width |
| 26 | `13` | field type `I32` |
| 27 | `01 00 00 00` | field name, length 1 |
| 31 | `"y"` | field name |
| 32 | `00` | bit width |
| 33 | `13` | field type `I32` |
| 34 | `71` | definition 1 |
| 35 | `06 00 00 00` | name, length 6 |
| 39 | `"st.Seg"` | name |
| 45 | `00` | layout |
| 46 | `03 00 00 00` | 3 fields |
| 50 | `01 00 00 00` | field name, length 1 |
| 54 | `"a"` | field name |
| 55 | `00` | bit width |
| 56 | `71 00 00 00 00` | field type: definition 0 |
| 61 | `01 00 00 00` | field name, length 1 |
| 65 | `"b"` | field name |
| 66 | `00` | bit width |
| 67 | `71 00 00 00 00` | field type: definition 0 |
| 72 | `03 00 00 00` | field name, length 3 |
| 76 | `"tag"` | field name |
| 79 | `00` | bit width |
| 80 | `52 11 01` | field type: array of `I8`, rank 1 |
| 83 | `01 ff ff ff ff 02 00 00 00 00 00 00 00` | extent 2 |
| 96 | `71 01 00 00 00` | the type: definition 1 |
| 101 | `01 00 00 00 02 00 00 00` | payload: `a.x` 1, `a.y` 2 |
| 109 | `03 00 00 00 04 00 00 00` | payload: `b.x` 3, `b.y` 4 |
| 117 | `07 08` | payload: `tag` 7, 8 |

<a id="a-checkpoint-with-an-arena-and-a-pool"></a>

### A.3 A checkpoint with an arena and a pool

1. A workbench session declares a pool and an arena, takes a handle into each and checkpoints the result:

```console
$ cint workbench --echo --out wb
cint workbench   profile cint-core-1   backend cpu-sir-interp   new session
ci[1]> Pool(I64, 4) pool;
pool : not shown by the session (it shows integers, Bool, and arrays of them)
ci[2]> Arena(I32, 8) ar;
ar : not shown by the session (it shows integers, Bool, and arrays of them)
ci[3]> Handle(I64) h;
h : not shown by the session (it shows integers, Bool, and arrays of them)
ci[4]> Handle(I32[]) a;
a : not shown by the session (it shows integers, Bool, and arrays of them)
ci[5]> void setup() { h = pool.insert(7); a = ar.alloc(3); ar[a][1] = 5; }
setup : () -> void   revision session@r5
ci[6]> setup();
ci[7]> :checkpoint one
checkpoint one   #1   bindings 2   revisions: session@r5
ci[7]> :save saved
saved session to saved   entries 6   checkpoints 1   objects 29
ci[7]> :quit --yes
```

2. The saved session keeps each object in a file named by the SHA-256 of its bytes. One of them is the checkpoint `one`, 481 bytes. Its state, from the global count on, is these bytes:

| Offset | Bytes | Field |
|---:|---|---|
| 0 | `02 00 00 00 00 00 00 00` | 2 globals |
| 8 | `09 00 00 00` | name, length 9 |
| 12 | `"session.a"` | name |
| 21 | `75` | tag: handle |
| 22 | `00 00 00 00 00 00 00 00` | slot 0 |
| 30 | `03 00 00 00 00 00 00 00` | length 3 |
| 38 | `00 00 00 00 00 00 00 00` | generation 0 |
| 46 | `01 00 00 00` | container: arena 1 |
| 50 | `01` | kind: arena |
| 51 | `09 00 00 00` | name, length 9 |
| 55 | `"session.h"` | name |
| 64 | `75` | tag: handle |
| 65 | `00 00 00 00 00 00 00 00` | slot 0 |
| 73 | `01 00 00 00 00 00 00 00` | length 1 |
| 81 | `00 00 00 00 00 00 00 00` | generation 0 |
| 89 | `01 00 00 00` | container: pool 1 |
| 93 | `02` | kind: pool |
| 94 | `5e 00 00 00 00 00 00 00` | arena block, length 94 |
| 102 | `14 00 00 00` | domain string, length 20 |
| 106 | `"cint-core-1/arena/v2"` | domain string |
| 126 | `01 00 00 00 00 00 00 00` | 1 arena |
| 134 | `01 00 00 00` | id 1 |
| 138 | `00 00 00 00` | parent: none |
| 142 | `00` | not retired |
| 143 | `00 00 00 00 00 00 00 00` | offset 0 |
| 151 | `08 00 00 00 00 00 00 00` | capacity 8 |
| 159 | `03 00 00 00 00 00 00 00` | used 3 |
| 167 | `00 00 00 00 00 00 00 00` | generation 0 |
| 175 | `04 00 00 00` | element width 4 |
| 179 | `00 00 00 00` | no definitions |
| 183 | `13` | element type `I32` |
| 184 | `00 00 00 00 05 00 00 00 00 00 00 00` | elements 0, 5, 0 |
| 196 | `5d 00 00 00 00 00 00 00` | pool block, length 93 |
| 204 | `13 00 00 00` | domain string, length 19 |
| 208 | `"cint-core-1/pool/v2"` | domain string |
| 227 | `01 00 00 00 00 00 00 00` | 1 pool |
| 235 | `01 00 00 00` | id 1 |
| 239 | `08 00 00 00` | element width 8 |
| 243 | `00 00 00 00` | no definitions |
| 247 | `14` | element type `I64` |
| 248 | `04 00 00 00 00 00 00 00` | capacity 4 |
| 256 | `01 00 00 00 00 00 00 00` | 1 slot used |
| 264 | `00 00 00 00 00 00 00 00` | free list: empty |
| 272 | `00 00 00 00 00 00 00 00` | slot 0: generation 0 |
| 280 | `01` | slot 0: occupied |
| 281 | `00 00 00 00 00 00 00 00` | slot 0: below 0 |
| 289 | `07 00 00 00 00 00 00 00` | element of slot 0: 7 |
| 297 | `00 00 00 00 00 00 00 00` | registry block: empty |
| 305 | `01 00 00 00 00 00 00 00` | next buffer identifier 1 |
| 313 | `02 00 00 00 00 00 00 00` | next arena identifier 2 |
| 321 | `02 00 00 00 00 00 00 00` | next pool identifier 2 |
| 329 | `01 00 00 00 00 00 00 00` | entry sequence number 1 |

3. The arena holds the three elements that `ar.alloc(3)` took, the second set to 5; the pool's one used slot holds 7. A session entry that is a declaration is not an entry of the context, so one entry, the call of `setup`, has begun.

<a id="recomputing-the-digest-chain"></a>

### A.4 Recomputing the digest chain

1. This Python program reads a case or a recording at entry density whose entries all ran, recomputes its digest chain from the rules of 8.5, and prints the head:

```python chain.py
import hashlib, struct, sys

def u(d, at, n): return int.from_bytes(d[at:at + n], "little"), at + n
def s(d, at): n, at = u(d, at, 4); return d[at:at + n], at + n
def b(d, at): n, at = u(d, at, 8); return d[at:at + n], at + n
def frame(t): return struct.pack("<I", len(t)) + t
DOMAIN = frame(b"cint-core-1/recording-chain/v1")

d = open(sys.argv[1], "rb").read()
at = 0
for _ in range(4): _, at = s(d, at)        # domain, profile, fuel model, contract
at += 32                                    # revision
at += 1 + 32 * d[at]                        # source-map digest
at += 1 + 8 + 1                             # mode, interval, density
mods, at = u(d, at, 4)
for _ in range(mods):
    _, at = s(d, at)
    _, at = b(d, at)
    at += 1 + (8 + u(d, at + 1, 8)[0] if d[at] else 0)
ckpt, at = b(d, at)
c = 4 + len(b"cint-core-1/checkpoint/v1")
_, c = s(ckpt, c)
c += 3                                      # three absent digests
state, c = b(ckpt, c)
chain = hashlib.sha256(DOMAIN + hashlib.sha256(state).digest()).digest()
index = entry = 0
while True:
    kind = d[at]; at += 1
    if kind == 1:
        entry = index
        at += 8 + 1
        _, at = s(d, at); _, at = s(d, at)
        n, at = u(d, at, 4)
        assert n == 0
        at += 24 + 4
    elif kind == 2:
        at += 8 + 8
        _, at = s(d, at)
        at += 4
        _, at = b(d, at)
    elif kind == 3:
        at += 4
        fault = None
        if d[at]:
            fault, at = s(d, at + 1)
        else:
            at += 1
        at += 8 + 1 + 1 + 4
        assert d[at] == 1
        h = d[at + 1:at + 33]; at += 33 + 8
        rec = struct.pack("<Q", entry) + b"\x00\x00" + h
        rec += b"\x00" if fault is None else b"\x01" + frame(fault)
        chain = hashlib.sha256(DOMAIN + chain + rec).digest()
    elif kind == 8:
        assert d[at + 1:at + 33] == chain
        break
    index += 1
print(chain.hex())
```

2. On the case of 8.6 it prints the chain that `cint debug replay` prints:

```console
$ python3 chain.py ledger.case
e54344d0fb4bcc2af778845380e8b60127eb639189fad8b9df78e45b60c34795
```

<a id="operation-fixtures-cif-1"></a>

## Annex B (normative) Operation fixtures, CIF-1

<a id="format"></a>

### B.1 Format

An operation fixture tests one operation on given operands, with no program around it. A file of fixtures in the format CIF-1 lists them, one a line.

**Syntax**

| Key | Content |
|---|---|
| `id` | The fixture's identifier, unique in its file, such as `div.checked.i64.007`. |
| `status` | `"S"`. |
| `op` | The operation identifier, with its rounding mode when the operation rounds. |
| `args` | An array of typed values, the operands in order. |
| `expect` | Exactly one of `{"value": typed value}`, `{"fault": fault}`, `{"compile_error": code}` and `{"reject": true}`. |
| `derivation` | Optional: a string that says how the expected result was derived. |

A typed value is `{"t": type, "v": integer}`, such as `{"t": "I64", "v": "-7"}`; a fixed-point value is `{"t": type, "raw": integer}`, its raw value, such as `{"t": "Q16.16", "raw": "98304"}`; an array is `{"t": "I8[3]", "v": ["120", "120", "-120"]}`. A fault is an object with the key `code` and, when the fixture asserts them, `exact`, `limit`, `index` and `operands`.

**Constraints**

1. <a id="IM-168"></a>A CIF-1 file is JSON Lines: UTF-8, one JSON object a line, each line ended by a line feed, with the keys in the order of the table [IM-168].
2. Every integer is a JSON string of decimal digits with an optional leading `-`, so that no JSON reader loses precision; RFC 8259 notes that JSON numbers are interchangeable exactly only between -(2^53 - 1) and 2^53 - 1 [IM-168].
3. <a id="IM-169"></a>A fault object has `code` and, when the record has them, `exact`, `limit`, `index` (for `fold_checked`) and `operands` (an array of typed values) [IM-169].

**Semantics**

4. <a id="IM-167"></a>Each fixture maps to a program: one that declares the operands with their types, applies the operation once, and whose expected outcome has the same operation identifier, fault code, operands, `exact` and `limit` [IM-167].
5. <a id="IM-170"></a>A fixture is frozen once published. A change of meaning takes a new group of fixtures under a new profile, never an edit; a change of encoding with the same content takes a new version of the format, and fixtures of the old version stay valid in it [IM-170].
6. Every implementation, the reference included, produces the expected result of every fixture. The reference computes in Python with exact integers and fractions and shares no code with the C implementation [IM-170].

**Examples**

7. Four fixtures, the third of fixed point and the fourth of `fold_checked`, which faults at index 1 when the running sum 120 + 120 leaves `I8`:

```console
$ cat sample.cif1.jsonl
{"id":"add.checked.i64.001","status":"S","op":"add.checked.i64","args":[{"t":"I64","v":"9223372036854775807"},{"t":"I64","v":"1"}],"expect":{"fault":{"code":"E_OVERFLOW","exact":"9223372036854775808","limit":"9223372036854775807"}}}
{"id":"rem.checked.i64.003","status":"S","op":"rem.checked.i64","args":[{"t":"I64","v":"7"},{"t":"I64","v":"-2"}],"expect":{"value":{"t":"I64","v":"-1"}}}
{"id":"mul.checked.q16_16.002","status":"S","op":"mul.checked.q16_16.half_even","args":[{"t":"Q16.16","raw":"1"},{"t":"Q16.16","raw":"98304"}],"expect":{"value":{"t":"Q16.16","raw":"2"}}}
{"id":"fold_checked.add.i8.001","status":"S","op":"fold_checked.add.i8","args":[{"t":"I8","v":"0"},{"t":"I8[3]","v":["120","120","-120"]}],"expect":{"fault":{"code":"E_OVERFLOW","exact":"240","limit":"127","index":"1"}}}
$ python3 -m cint_ref cif1 sample.cif1.jsonl
cif1: 4 records, 4 agree, 0 disagree, 0 unsupported, 0 invalid
```

8. The remainder of 7 by -2 is -1, because `%` takes the quotient rounded toward negative infinity (SPEC-04P.3 6.7.7). The `Q16.16` values 2^-16 (raw 1) and 1.5 (raw 98304) have the product 1.5 times 2^-16, the raw value 1.5, which rounds half to even to the raw value 2.

<a id="clause-index"></a>

## Annex C (informative) Clause index

The clause identifiers of SPEC-01P.1 and where their rules are in the P editions; an identifier that the table does not list has no rule in them. SPEC-04P.3 sections are given by number; where a clause is split, each part is listed.

| Clauses of SPEC-01P.1 | Where they are now |
|---|---|
| <a id="IM-1"></a>IM-1 | SPEC-04P.3 6.1 |
| IM-2, IM-3 | Not carried: they divided the rules of SPEC-01P.1 between its sections. |
| <a id="IM-4"></a><a id="IM-6"></a>IM-4, IM-6 | SPEC-04P.3 6.3.1 |
| <a id="IM-5"></a>IM-5 | SPEC-04P.3 6.2 |
| <a id="IM-7"></a><a id="IM-8"></a>IM-7, IM-8 | SPEC-04P.3 6.3.1; their encoding part in 5.1 here |
| <a id="IM-9"></a><a id="IM-10"></a><a id="IM-11"></a>IM-9 to IM-11 | SPEC-04P.3 6.3 |
| IM-12, IM-13 | SPEC-04P.3 6.3.1 for the types; their bytes in 5.2 here |
| IM-14 | 5.2 here; the C interface in SPEC-03P.2 |
| IM-15 | Not carried: it described a layout that `cint-core-1` does not use. |
| <a id="IM-16"></a>IM-16 to IM-18 | SPEC-04P.3 6.3.1 |
| <a id="IM-19"></a><a id="IM-20"></a>IM-19, IM-20 | SPEC-04P.3 6.4 |
| <a id="IM-21"></a><a id="IM-22"></a><a id="IM-23"></a><a id="IM-24"></a><a id="IM-25"></a><a id="IM-26"></a><a id="IM-27"></a>IM-21 to IM-27 | SPEC-04P.3 6.5 and 6.8 |
| <a id="IM-28"></a><a id="IM-29"></a><a id="IM-30"></a><a id="IM-31"></a><a id="IM-32"></a><a id="IM-33"></a><a id="IM-34"></a>IM-28 to IM-34 | SPEC-04P.3 6.7 |
| <a id="IM-35"></a><a id="IM-36"></a><a id="IM-37"></a><a id="IM-39"></a><a id="IM-40"></a><a id="IM-41"></a>IM-35 to IM-41 | SPEC-04P.3 6.7 and 7.2 |
| <a id="IM-42"></a><a id="IM-43"></a><a id="IM-44"></a><a id="IM-45"></a><a id="IM-46"></a><a id="IM-47"></a><a id="IM-48"></a><a id="IM-49"></a>IM-42 to IM-49 | SPEC-04P.3 6.7 |
| <a id="IM-50"></a>IM-50 | SPEC-04P.3 6.6 |
| <a id="IM-51"></a><a id="IM-52"></a><a id="IM-53"></a>IM-51 to IM-53 | SPEC-04P.3 7.2 |
| <a id="IM-54"></a><a id="IM-55"></a><a id="IM-56"></a><a id="IM-57"></a>IM-54 to IM-59 | SPEC-04P.3 6.3.2 |
| <a id="IM-60"></a><a id="IM-61"></a><a id="IM-62"></a>IM-60 to IM-62 | SPEC-04P.3 6.7 |
| <a id="IM-63"></a><a id="IM-64"></a>IM-63, IM-64 | SPEC-04P.3 6.7 and 7.2 |
| <a id="IM-65"></a><a id="IM-66"></a>IM-65, IM-66 | SPEC-04P.3 6.7 |
| <a id="IM-67"></a>IM-67 | SPEC-04P.3 7.2 |
| <a id="IM-68"></a><a id="IM-69"></a>IM-68, IM-69 | SPEC-04P.3 6.6 |
| IM-70 to IM-72 | SPEC-03P.2 |
| <a id="IM-74"></a><a id="IM-75"></a><a id="IM-76"></a><a id="IM-77"></a><a id="IM-78"></a><a id="IM-79"></a><a id="IM-80"></a><a id="IM-81"></a><a id="IM-82"></a><a id="IM-83"></a><a id="IM-84"></a><a id="IM-85"></a><a id="IM-86"></a><a id="IM-87"></a><a id="IM-90"></a>IM-73 to IM-91 | SPEC-04P.3 7.3 |
| <a id="IM-92"></a><a id="IM-93"></a><a id="IM-94"></a><a id="IM-95"></a><a id="IM-96"></a><a id="IM-97"></a><a id="IM-98"></a><a id="IM-99"></a><a id="IM-100"></a><a id="IM-101"></a><a id="IM-102"></a><a id="IM-103"></a>IM-92 to IM-103 | SPEC-04P.3 6.2; IM-103 also 6.8 |
| <a id="IM-104"></a>IM-104 | SPEC-04P.3 5.2.3 and Annex B |
| <a id="IM-105"></a>IM-105 | SPEC-04P.3 5.2 |
| <a id="IM-106"></a><a id="IM-107"></a>IM-106, IM-107 | SPEC-04P.3 8.1; the bounds of fields in 6.1 here |
| IM-108 | 6.3 here |
| <a id="IM-111"></a><a id="IM-112"></a><a id="IM-113"></a><a id="IM-114"></a><a id="IM-115"></a><a id="IM-116"></a>IM-110 to IM-116 | SPEC-04P.3 8.1 and 8.2 |
| <a id="IM-117"></a><a id="IM-118"></a><a id="IM-119"></a><a id="IM-120"></a><a id="IM-121"></a><a id="IM-122"></a>IM-117 to IM-122 | SPEC-04P.3 5.2 |
| <a id="IM-123"></a><a id="IM-124"></a><a id="IM-125"></a><a id="IM-126"></a>IM-123 to IM-126 | SPEC-02P.2 10.1 |
| IM-127 | SPEC-04P.3 5.2 |
| <a id="IM-128"></a>IM-128 | SPEC-04P.3 Annex C |
| <a id="IM-129"></a>IM-129 | SPEC-04P.3 clause 4 |
| <a id="IM-130"></a><a id="IM-131"></a><a id="IM-132"></a><a id="IM-133"></a><a id="IM-134"></a><a id="IM-135"></a>IM-130 to IM-136 | SPEC-04P.3 Annex B |
| <a id="IM-137"></a><a id="IM-139"></a><a id="IM-140"></a><a id="IM-141"></a><a id="IM-142"></a><a id="IM-143"></a>IM-137 to IM-143 | SPEC-04P.3 5.2 |
| IM-144 | 5.1 here |
| IM-145 | 5.2 and 5.3 here |
| IM-146, IM-147 | 5.4 here |
| IM-148 | Clause 4, 5.1, 5.3 to 5.5, 6.3 and 6.4 here |
| IM-149 | 6.1, 6.3 and 6.4 here |
| IM-150, IM-151 | 7.1 here; IM-151 also 9.4 |
| IM-152 | 5.1 here |
| IM-153 | 5.1 here |
| IM-154 | 7.6 here |
| IM-155 | 7.1 here |
| IM-156 | 7.2 and 7.6 here |
| IM-157 | 9.1 and 9.4 here |
| IM-158 | Clause 10 here: the arguments digest is not written by cint 0.2.2. |
| <a id="IM-159"></a>IM-159 | SPEC-04P.3 8.2; their canonical form in a recording in 9.3 here |
| <a id="IM-160"></a>IM-160 | SPEC-04P.3 8.2 |
| IM-166 | 6.1 here: a record has the same bytes on every executor. |
| IM-167 to IM-170 | Annex B here |
| IM-171 | SPEC-09P.2, as cases of the conformance suite |
| IM-172 to IM-183 | Not carried: each document's clause 1 states its own scope. |
| <a id="IM-184"></a><a id="IM-185"></a>IM-184, IM-185 | SPEC-04P.3 Annex B |
| <a id="IM-186"></a><a id="IM-187"></a>IM-186, IM-187 | SPEC-04P.3 6.12 |
| <a id="IM-188"></a>IM-188 | SPEC-04P.3 6.7 |
| IM-189 | SPEC-04P.3 7.3 |
