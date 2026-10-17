# Operation measurements

The benchmark times synthetic operations on in-memory images. Loading, saving,
initial image allocation, initialization, output hashing and protocol messages
are outside the timed region. These results do not measure end-to-end editor
performance; SAVE also performs synchronization and filesystem work.

Run the current implementation without Git history or grading assets:

```sh
make benchmark
```

## Setup

- Baseline: monolithic implementation at development checkpoint `cc9565c` (P0).
- Candidate: modular `image.c` and `operations.c` implementation (P1).
- Compiler: GCC 11.4.0 on x86-64 Linux 6.8.0, glibc 2.35.
- Both versions: `-std=c99 -O2 -D_POSIX_C_SOURCE=200809L`, no sanitizers or LTO.
- Deterministic 2048x2048 RGB pattern; three repeated operations per trial.
- One warm-up trial, then seven trials per workload/version, alternating order.
- `CLOCK_PROCESS_CPUTIME_ID`; reported value is median CPU seconds per operation.
- Matching FNV-1a output fingerprints are required across versions and trials.

The comparison below was run on 2026-10-18. Repeating it requires a checkout
that retains `cc9565c`; the source-only `main` history does not include that
checkpoint. The script reads its source with `git show` and compiles both
versions using the same flags.

```sh
mkdir -p build/p1
python3 -B tools/benchmark.py --baseline-ref cc9565c \
  --size 2048 --iterations 3 --trials 7 --report build/p1/benchmark.json
```

| Workload | P0 median (ms) | P1 median (ms) | P0 / P1 |
| --- | ---: | ---: | ---: |
| Full-image 180° rotation | 16.552 | 4.131 | 4.01 |
| Blur selected 128x128 interior region | 8.771 | 0.381 | 22.99 |
| Blur entire image | 111.450 | 101.500 | 1.10 |

Raw seconds per operation, in trial order:

| Workload/version | Seven samples |
| --- | --- |
| Rotation P0 | .017150873, .016551641, .016713774, .016553907, .016419461, .015700328, .015687235 |
| Rotation P1 | .004343822, .004257165, .004131285, .004317533, .003833178, .004087857, .003851225 |
| Selected blur P0 | .008770961, .008735655, .008749748, .009778397, .009115865, .008990882, .008743077 |
| Selected blur P1 | .000379051, .000478126, .000394270, .000381469, .000392356, .000380615, .000372665 |
| Full blur P0 | .117499147, .112604068, .110293702, .110510525, .111449790, .111163710, .112006264 |
| Full blur P1 | .102239612, .100760861, .101631782, .101488728, .103108788, .101499546, .101081256 |

No CPU affinity, frequency isolation, RSS measurement or confidence intervals
were used. Scheduling, allocator state, cache effects and memory bandwidth can
affect other runs. Avoiding allocation and full-image copying explains the large
differences for rotation and a small selected blur. The modest full-blur
difference may not hold on other machines or workloads.

## Why buffer usage changes

P0 allocates a full destination for 180° rotation; P1 swaps symmetric row-major
pixels in place. P0 filters allocate/copy the whole image even for a small
selection. P1 stores only changed interior pixels, completes computation from the
original image, then commits selected rows back into the existing allocation.

For the benchmark's 2048x2048 RGB image with three-byte pixels:

| Operation | P0 extra heap buffer | P1 extra heap buffer |
| --- | ---: | ---: |
| Full 180° rotation | 12,582,912 bytes | 0 bytes |
| Blur selected 128x128 interior region | 12,582,912 bytes | 49,152 bytes |
| Full blur | 12,582,912 bytes | 12,558,348 bytes |

These sizes are derived from allocation formulas, not measured process RSS.
The linked failure-injection harness additionally checks zero allocation for a
180° rotation, a single `rgb` allocation for a 3x3 image's one interior filter
pixel, and unchanged original image-buffer identity.

## Functional comparison

The benchmark checks final pixel hashes; the independent regressions use exact
small-image oracles. To compare generated editing pipelines, first compile a
baseline in a checkout retaining `cc9565c`:

```sh
mkdir -p build/p1
git show cc9565c:image_editor.c > build/p1/p0.c
gcc -std=c99 -O2 -D_POSIX_C_SOURCE=200809L \
  build/p1/p0.c -o build/p1/p0-editor
make release
```

Then run:

```sh
python3 -B tools/compare_p0.py --baseline ./build/p1/p0-editor \
  --candidate ./build/release/image_editor
```

This compares stdout and every saved byte for 32 deterministic valid pipelines
covering all four formats, grayscale/RGB, Maxval 15/255, all filters, histogram,
equalization, square selections, full rotations and cropping. Its fixtures are
generated locally and contain no assignment data. A separately preserved P0
executable can also be passed to `--baseline`; the historical executable is not
part of the source distribution.
