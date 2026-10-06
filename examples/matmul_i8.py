"""matmul_i8.py: I8 activations times ternary weights, in CINT, from Python.

Run it from the repository root, with NumPy installed and a bootstrapped
`cint` on PATH (or named by CINT_EXE):

    PYTHONPATH=python python3 examples/matmul_i8.py

Tutorial 04 (docs/tutorials/04-python-matmul.md) walks through it. The first
run builds examples/matmul_i8.ci into a shared library with
`cint build --lib`; later runs load the cached library.
"""
import os

import numpy as np

import cint

HERE = os.path.dirname(os.path.abspath(__file__))


def inputs():
    """Four rows of 16 activations from -8 to 8, the last row scaled by 15 so
    that it reaches 120, and 16 by 3 weights of -1, 0 or 1. No randomness: the
    same arrays on every machine."""
    i = np.arange(4)[:, None]
    p = np.arange(16)[None, :]
    x = ((i * 5 + p * 3) % 17 - 8).astype(np.int8)
    x[3] *= 15
    w = ((np.arange(16)[:, None] + 2 * np.arange(3)[None, :]) % 3 - 1).astype(np.int8)
    return x, w


def main():
    mod = cint.load(os.path.join(HERE, "matmul_i8.ci"))
    ctx = mod.context()
    x, w = inputs()

    # 1. The product in I32. x and w are borrowed for the call; y is borrowed
    #    writable and the function writes it in place.
    y = np.zeros((4, 3), dtype=np.int32)
    ctx.matmul(x, w, y=cint.borrow(y, writable=True))
    expected = x.astype(np.int64) @ w.astype(np.int64)
    print("y = x @ w:", y.tolist())
    print("equal to NumPy's int64 product:", bool((y == expected).all()))
    print(ctx.last_entry())

    # 2. A kernel into a Buffer that the bridge owns: the runtime stages the
    #    output and copies it on success.
    z = cint.empty((4, 3), "I32")
    ctx.relu(y, z=z)
    print("relu(y):", z.tolist())
    print(ctx.last_entry())

    # 3. The same kernel into NumPy memory: the caller asks for the copy.
    z2 = np.full((4, 3), -1, dtype=np.int32)
    ctx.relu(y, z=cint.borrow(z2, writable=True, publish="copy"))
    print("relu(y) into NumPy:", z2.tolist())
    print(ctx.last_entry())

    # 4. The product kept in I8. Row 3 holds 300, which I8 cannot.
    y8 = np.zeros((4, 3), dtype=np.int8)
    try:
        ctx.matmul_narrow(x, w, y=cint.borrow(y8, writable=True))
    except cint.OverflowFault as fault:
        print("matmul_narrow faulted:", fault.code, fault.operation, "at", fault.position)
        print(fault)
        print("y8 after the fault:", y8.tolist())
        print("the context holds the fault:", ctx.fault() is not None)
        ctx.clear_fault()
    print("y after the fault:", y.tolist())


if __name__ == "__main__":
    main()
