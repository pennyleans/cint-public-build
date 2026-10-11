//! Rust baselines for the two cpu_trials workloads, built as a C-ABI shared
//! library and called from run_rust_dotnet.py exactly as the C baselines are.
//!
//! Three forms of the same loops:
//! - `rust_checked_*`: the explicit `checked_add` and `checked_sub` methods,
//!   returning status 1 on overflow and leaving `out` unchanged. Same semantics
//!   as baseline.c. Independent of the build's overflow-checks setting.
//! - `rust_ops_*`: the operator form (`b = b + i % 97`). With
//!   `-C overflow-checks=on` an overflow panics, caught at the boundary with
//!   `catch_unwind` and reported as status 1. With `-C overflow-checks=off`
//!   (the release profile default) an overflow wraps silently and the function
//!   returns status 0 with the wrapped value: a speed ceiling, not a checked
//!   baseline.
//!
//! `rem_euclid` gives the floor remainder CINT's `%` gives for a negative
//! seed; `i` and `j` are never negative in the hot loops.

use std::panic::{catch_unwind, AssertUnwindSafe};

#[no_mangle]
pub extern "C" fn rust_checked_ledger(n: i64, initial: i64, out: &mut i64) -> i32 {
    let mut b = initial;
    let mut i: i64 = 0;
    while i < n {
        b = match b.checked_add(i % 97) { Some(v) => v, None => return 1 };
        b = match b.checked_sub(48) { Some(v) => v, None => return 1 };
        i = match i.checked_add(1) { Some(v) => v, None => return 1 };
    }
    *out = b;
    0
}

#[no_mangle]
pub extern "C" fn rust_checked_transactions(n: i64, initial: i64, seed: i64, out: &mut i64) -> i32 {
    let mut amounts = [0i64; 256];
    for j in 0..256i64 {
        let x = match j.checked_mul(17) { Some(v) => v, None => return 1 };
        let x = match seed.checked_add(x) { Some(v) => v, None => return 1 };
        let x = x.rem_euclid(201);
        amounts[j as usize] = match x.checked_sub(100) { Some(v) => v, None => return 1 };
    }
    let mut b = initial;
    let mut i: i64 = 0;
    while i < n {
        b = match b.checked_add(amounts[(i % 256) as usize]) { Some(v) => v, None => return 1 };
        i = match i.checked_add(1) { Some(v) => v, None => return 1 };
    }
    *out = b;
    0
}

fn ops_ledger(n: i64, initial: i64) -> i64 {
    let mut b = initial;
    let mut i: i64 = 0;
    while i < n {
        b = b + i % 97;
        b = b - 48;
        i = i + 1;
    }
    b
}

fn ops_transactions(n: i64, initial: i64, seed: i64) -> i64 {
    let mut amounts = [0i64; 256];
    for j in 0..256i64 {
        amounts[j as usize] = (seed + j * 17).rem_euclid(201) - 100;
    }
    let mut b = initial;
    let mut i: i64 = 0;
    while i < n {
        b = b + amounts[(i % 256) as usize];
        i = i + 1;
    }
    b
}

#[no_mangle]
pub extern "C" fn rust_ops_ledger(n: i64, initial: i64, out: &mut i64) -> i32 {
    match catch_unwind(AssertUnwindSafe(|| ops_ledger(n, initial))) {
        Ok(v) => { *out = v; 0 }
        Err(_) => 1,
    }
}

#[no_mangle]
pub extern "C" fn rust_ops_transactions(n: i64, initial: i64, seed: i64, out: &mut i64) -> i32 {
    match catch_unwind(AssertUnwindSafe(|| ops_transactions(n, initial, seed))) {
        Ok(v) => { *out = v; 0 }
        Err(_) => 1,
    }
}

/// 1 when this library was built with overflow checks, else 0: probed by
/// adding one to i64::MAX in operator form (a panic means checks are on).
#[no_mangle]
pub extern "C" fn rust_overflow_checks() -> i32 {
    let max = std::hint::black_box(i64::MAX);
    match catch_unwind(|| max + 1) {
        Ok(_) => 0,
        Err(_) => 1,
    }
}

/// Silence the default panic message so a caught overflow panic in the
/// validation step does not write to stderr. Called once by the runner.
#[no_mangle]
pub extern "C" fn rust_quiet_panics() {
    std::panic::set_hook(Box::new(|_| {}));
}
