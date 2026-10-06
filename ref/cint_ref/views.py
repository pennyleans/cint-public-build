"""Views: layouts, slicing and the aliasing tests of SPEC-02 section 5 (box 09).

A view is described by its origin, shape and strides, in elements, over a buffer
(SPEC-02 V-1, V-4). Every declared lower bound is 0: `cint_ref` refuses
declared lower bounds. These functions take plain integers and tuples, so the
checker uses them on static descriptors and the executor on run-time ones.
"""
from __future__ import annotations

import math


def row_major(shape) -> tuple:
    """The strides of a row-major array of `shape`: the last index varies fastest (SPEC-04 LS-63)."""
    strides, step = [], 1
    for n in reversed(shape):
        strides.append(step)
        step *= n
    return tuple(reversed(strides))


def count(shape) -> int:
    n = 1
    for e in shape:
        n *= e
    return n


def offsets(origin, shape, strides):
    """Element offsets in row-major logical order (SPEC-01 IM-74, SPEC-02 V-4)."""
    if count(shape) == 0:
        return
    index = [0] * len(shape)
    while True:
        yield origin + sum(i * s for i, s in zip(index, strides))
        k = len(shape) - 1
        while k >= 0:
            index[k] += 1
            if index[k] < shape[k]:
                break
            index[k] = 0
            k -= 1
        if k < 0:
            return


def slice_dim(extent, lo, hi, inclusive, step):
    """One slice item over a dimension of `extent` (SPEC-04 LS-161; box 09 rulings R2, R3).
    `lo` and `hi` are the bounds as written, None where omitted; `step` is nonzero.
    Returns (valid, first, length, lo, hi), where `lo` and `hi` are the bounds after the
    omitted ones are filled in (the operands of `slice.checked.<E>`) and `first` is the
    index of the view's first element."""
    if step > 0:
        lo = 0 if lo is None else lo
        hi = extent if hi is None else hi
        end = hi + 1 if inclusive else hi          # a[lo..=hi] is a[lo..hi + 1], exactly
        valid = 0 <= lo <= end <= extent
        length = (end - lo + step - 1) // step if valid else 0
        return valid, lo, length, lo, hi
    k = -step
    first = extent - 1 if lo is None else lo
    if inclusive:
        last = 0 if hi is None else hi
        valid = 0 <= last <= first + 1 <= extent
        length = (first - last + 1 + k - 1) // k if valid and first >= last else 0
        return valid, first, length, first, last
    last = -1 if hi is None else hi                # "through the first index"
    valid = -1 <= last <= first <= extent - 1
    length = (first - last + k - 1) // k if valid else 0
    return valid, first, length, first, last


def bounds(origin, shape, strides):
    """The bounding interval [lo, hi] of a nonempty view (SPEC-02 V-5)."""
    lo = origin + sum(s * (n - 1) for n, s in zip(shape, strides) if s < 0)
    hi = origin + sum(s * (n - 1) for n, s in zip(shape, strides) if s > 0)
    return lo, hi


def classify(p, q) -> str:
    """The pairwise decision of SPEC-02 A-5 for two views (buffer, origin, shape, strides):
    `disjoint`, `identical` (T3), `overlap` (T4), or `uncertain`. Buffers are compared by
    key: the executor passes `id()` of the buffer list, the checker a static key."""
    pb, po, ps, pt = p
    qb, qo, qs, qt = q
    if count(ps) == 0 or count(qs) == 0:
        return "disjoint"                                            # T0
    if pb != qb:
        return "disjoint"                                            # T1: different buffers
    plo, phi = bounds(po, ps, pt)
    qlo, qhi = bounds(qo, qs, qt)
    if phi < qlo or qhi < plo:
        return "disjoint"                                            # T1
    g = 0
    for shape, strides in ((ps, pt), (qs, qt)):
        for n, s in zip(shape, strides):
            if n > 1:
                g = math.gcd(g, abs(s))
    if g >= 2 and po % g != qo % g:
        return "disjoint"                                            # T2
    if po == qo and tuple(ps) == tuple(qs) and tuple(pt) == tuple(qt):
        return "identical"                                           # T3
    if count(ps) == 1 and count(qs) == 1 and po == qo:
        return "overlap"                                             # T4
    return "uncertain"


def injective(shape, strides) -> bool:
    """The sufficient injectivity test of SPEC-02 A-7."""
    if count(shape) == 0:
        return True
    dims = sorted((abs(s), k) for k, (n, s) in enumerate(zip(shape, strides)) if n != 1)
    reach = 0
    for a, k in dims:
        if a < 1 + reach:
            return False
        reach += a * (shape[k] - 1)
    return True
