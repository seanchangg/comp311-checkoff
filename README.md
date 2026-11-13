# SIMD and Vectorization

COMP 211 Checkoff 3. Topic: Compiler Optimizations, Vectorization.

## Topic

SIMD means "single instruction, multiple data". A SIMD instruction does the same operation on several values at the same time. It uses special wide registers and SIMD units in the CPU.

- On my Mac (Apple M4, ARM NEON), the SIMD registers are `v0`–`v31`. Each one is 128 bits wide, so one `add.4s` instruction adds four 32-bit ints.
- On x86 CPUs, the SIMD registers are 128-bit (`xmm`, SSE), 256-bit (`ymm`, AVX2), or 512-bit (`zmm`, AVX-512).
- The general-purpose registers `x0`–`x30` hold one 64-bit value each.

Vectorization is the compiler optimization that turns a normal loop into SIMD instructions. I explored when the compiler does this, how much it helps, and when it cannot help.

## What I explored

- How the optimization levels `-O0` to `-O3` change the run time of two C programs.
- Which programs fit SIMD well and which do not.
- How to read the assembly and find the SIMD instructions.
- What the compiler does to a benchmark when it can prove that the work is not used.

## Files

| File | Contents |
|---|---|
| `goodprogram.c` | Example 1. Adds two arrays of 1,000,000 ints in order. This fits SIMD well. |
| `badprogram.c` | Example 2. Adds the same arrays in a shuffled order. This does not fit SIMD well. |
| `COMP211 Checkoff 3 Slides.pdf` | My presentation slides. |

## How to run

You need a C compiler (clang or gcc). I used Apple clang 17.0.0 on an Apple M4 Mac.

Compile and run one program:

```bash
clang -O2 goodprogram.c -o good
./good
```

Each program prints the average time for one pass over the arrays, for example `Average Time: 97.3832000000 microseconds`.

Compare all four optimization levels:

```bash
for L in 0 1 2 3; do clang -O$L goodprogram.c -o good && echo "-O$L: $(./good)"; done
```

`badprogram.c` at `-O0` takes about 25 seconds, because it does 10,000 passes.

See the assembly (look for `add.4s` on ARM, or `paddd`/`vpaddd` on x86):

```bash
clang -O2 -S goodprogram.c -o goodprogram.s
```

Ask clang which loops it vectorized:

```bash
clang -O2 -c -Rpass=loop-vectorize goodprogram.c
```

It prints `vectorized loop (vectorization width: 4, interleaved count: 4)`. For `badprogram.c`, it prints only `interleaved loop (interleaved count: 4)`, so that loop stays scalar.

Turn vectorization off to measure what SIMD alone gives:

```bash
clang -O2 -fno-vectorize -fno-slp-vectorize goodprogram.c -o good_scalar
```

## Results

Average time for one pass, on an Apple M4 with Apple clang 17.0.0. Each time is the median of 3 runs. For Example 1 at `-O2` and `-O3`, it is the median of 9 runs, because single runs varied from 84.73 to 108.30 µs.

| Level | Example 1, in order (µs) | Example 2, shuffled (µs) |
|---|---|---|
| `-O0` | 903.17 | 2347.43 |
| `-O1` | 260.51 | 1753.33 |
| `-O2` | 100.00 | 1777.84 |
| `-O3` | 99.99 | 1813.72 |

- **The vectorization step:** From `-O1` to `-O2`, Example 1 gets 2.6× faster. Clang vectorizes loops at `-O2` and `-O3`, but not at `-O1`.
- **No gain from -O3:** For both programs, clang makes the same assembly at `-O2` and `-O3`. The small differences between those two columns are noise.
- **No SIMD for Example 2:** The loads go to random places in memory. NEON has no "gather" load for scattered elements, so the loop stays scalar.
- **Cache misses:** Example 2 is slow mostly because of cache misses. At `-O1`, neither program uses SIMD, but Example 2 is still 6.7× slower than Example 1.

## Most interesting discoveries

### 1. SIMD helps most when the data is in cache

I first thought that SIMD helps more with large arrays. My test showed the opposite. I compiled Example 1 at `-O2` with and without vectorization, at four array sizes (median of 5 runs):

| Array size | Scalar (µs per pass) | SIMD (µs per pass) | Speedup |
|---|---|---|---|
| 4,096 ints (48 KB, fits in L1) | 1.03 | 0.22 | 4.73× |
| 262,144 ints (3 MB) | 66.92 | 25.40 | 2.63× |
| 1,000,000 ints (12 MB) | 259.76 | 99.20 | 2.62× |
| 16,000,000 ints (192 MB, main memory) | 4245.38 | 2215.24 | 1.92× |

SIMD cuts the number of instructions. It does not cut the number of bytes that come from memory. With large arrays, the loop waits for memory, so the gain from SIMD gets smaller.

### 2. The compiler deletes work that nothing uses

My first benchmark printed a time of 0 at `-O2`. Nothing read the arrays after the loop, so the compiler deleted the loop (dead code elimination). The C "as-if" rule allows this: the compiler must keep only the observable behavior of the program. In another test, the compiler calculated a sum without a loop over the arrays at all.

The fix is a compiler barrier:

```c
static inline void keep(void *p) { __asm__ volatile("" : : "r"(p) : "memory"); }
```

- The assembly text is empty, so `keep()` adds zero instructions.
- `"r"(p)` and `"memory"` tell the compiler that something can read or change the array.
- So the compiler must finish all stores before `keep()`, and it must do the full loop again in the next pass.

`keep()` does not make the arrays `volatile`. A `volatile` array forces a separate load and store for each element, and that blocks vectorization. `keep()` puts a barrier only between passes, so the inner loop can still use SIMD.

### 3. Predictable values get calculated, not loaded

When I filled the arrays with a pattern such as `10*i`, the compiler did not load or calculate each value separately. It kept a running value in a register and added a fixed step for each element (strength reduction). To measure real loads, I filled the arrays with `rand()`.

### 4. Four SIMD adds in each loop pass

At `-O2` and `-O3`, the vector loop has four `add.4s` instructions, so one pass adds 16 ints. The compiler unrolls the loop 4 times to cut the loop overhead and to give the CPU independent work. LLVM sets a limit of 4 copies for Apple CPUs. When I forced more copies, small arrays got faster, but large arrays did not.

## Resources

- [Auto-Vectorization in LLVM](https://llvm.org/docs/Vectorizers.html)
- [Clang User's Manual: optimization reports (`-Rpass`)](https://clang.llvm.org/docs/UsersManual.html#options-to-emit-optimization-reports)
- [GCC: Extended Asm](https://gcc.gnu.org/onlinedocs/gcc/Extended-Asm.html), which explains the `"memory"` clobber in `keep()`
- [cppreference: the as-if rule](https://en.cppreference.com/w/c/language/as_if)
- [Compiler Explorer](https://godbolt.org/), to see the assembly for different compilers and flags
- [Wikipedia: Single instruction, multiple data](https://en.wikipedia.org/wiki/Single_instruction,_multiple_data)
- [Wikipedia: Automatic vectorization](https://en.wikipedia.org/wiki/Automatic_vectorization)
- [Wikipedia: Strength reduction](https://en.wikipedia.org/wiki/Strength_reduction)
- Claude (Anthropic), which I used as a tutor. My written reflection lists the prompts.
