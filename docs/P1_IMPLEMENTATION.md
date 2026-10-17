# Design and testing

## Ownership and module boundaries

`image_editor.c` passes standard streams to `editor_run`. The command layer owns
the session's image and borrows its streams, leaving them open on return.
`netpbm` handles files; `operations` transforms buffers without terminal I/O.
Both return `image_status`, and the command layer chooses the diagnostic.

Initialize an `image` with `{0}`. A live image owns one contiguous allocation;
copying the struct does not copy its buffer. `image_destroy` frees the buffer
and zeros the struct. Samples must be initialized within `[0,Maxval]` before
use. `image_valid` checks metadata, not an external buffer's capacity.

Grayscale/RGB format is independent of ASCII/binary encoding. The data pointer
determines whether an image is loaded; selection coordinates determine whether
the whole image is selected. Dimensions must be positive and fit `int`.
`image_sizes` checks multiplication against `SIZE_MAX`, pixel count against
`INT_MAX`, and byte count against `PTRDIFF_MAX`. Count products for histograms
and equalization use bounded `uint64_t` arithmetic; indexes use `size_t`.

Fallible edits allocate before replacing an image. Failed edits preserve data
and selection. `netpbm_load` also preserves an initialized destination on
failure; the CLI discards it before a valid LOAD attempt to match the coursework
protocol. Invalid syntax never discards an image. Recognized commands without
an image retain the `No image loaded` diagnostic precedence.

## Pixel operations and parsing

Cropping copies selected rows into a new buffer. Full quarter-turn rotations
map source coordinates into a new buffer and swap dimensions. Square selections
rotate in place through four-way pixel swaps. Full 180° rotation reverses the
row-major pixel sequence without allocating another image.

Filters calculate only the selected interior into scratch storage, then copy
the completed rows back. This prevents one output pixel from affecting another
pixel's convolution. The outer image border is unchanged; neighbors outside
the selection remain valid inputs. Integer kernel sums round nonnegative
fractions to nearest with ties upward, then clamp to `[0,Maxval]`.
Fixed kernels and 256-entry histogram/lookup tables need no heap allocation.
See [measurements](PERFORMANCE.md) for timing and buffer-size comparisons.

Equalization evaluates `(CDF_count * Maxval + N / 2) / N` with integer
arithmetic. It does not subtract the minimum CDF; constant images map to Maxval.
This matches the supplied reference fixture. The original assignment statement
is unavailable, so its specification has not been independently verified.
Histograms use proportional bins and truncate scaled star counts toward zero.

Command integers use complete, range-checked `strtol` conversion. NetPBM numeric
tokens are limited to 63 decimal digits; header and plain-raster comments end
at CR or LF. Raw decoding consumes exactly one whitespace byte after Maxval,
preserving any whitespace or `#` sample bytes that follow. See the
[PGM](https://netpbm.sourceforge.net/doc/pgm.html) and
[PPM](https://netpbm.sourceforge.net/doc/ppm.html) format definitions.
RGB channels are serialized explicitly, independently of struct padding.

## Saving

1. Inspect the destination with `lstat`; reject an existing final symlink or
   nonregular file.
2. Use `mkstemp` in the destination directory and transfer the descriptor to
   a stream with `fdopen`.
3. Write the header/raster, check `fflush`, preserve existing rwx permissions,
   then check `fsync` and `fclose`.
4. Rename the complete temporary file over the destination.
5. On an earlier failure, attempt to unlink the temporary file and release
   its resources.

Same-directory creation keeps replacement on one filesystem. On a local POSIX
filesystem, readers see a complete old or new file. The parent directory is not
synced, so successful SAVE does not establish full power-loss durability.

New files request mode `0600`, subject to umask; replacements retain existing
ordinary rwx permissions. Ownership, ACLs, extended attributes, privileged mode
bits and hard-link identity are not preserved. Directory permissions govern
replacement, even when the destination's file mode is read-only. LOAD may follow
symlinks; SAVE rejects an existing final symlink.

Concurrent writers are not coordinated. Separate path checks and rename do not
protect against adversarial directory changes. Termination or changed cleanup
permissions can leave a temporary file. Network filesystem error semantics and
Windows support have not been verified.

The command loop checks stream error indicators and the final output flush.
An earlier write failure remains an error even if that flush succeeds. Tests
cover both short output and `HISTOGRAM 10000 2` redirected to `/dev/full`.

## Testing

The [README](../README.md#tests) lists the independent test commands. Python
fixtures exercise exact pixels, dimensions, messages, encodings, malformed input,
command boundaries and file behavior. The C harness uses GNU linker `--wrap`
interposition to inject allocation, short-write, flush, sync, mode-change, close,
temporary-file, stream-conversion and rename failures. It checks unchanged
buffers/destinations, temporary-file cleanup and Linux descriptor counts through
`/proc/self/fd`. Production code contains no test hooks.

Build modes use `-Og -g3`, `-O2`, and ASan/UBSan with `-O1 -g3`, respectively.
Sanitizer flags apply at compilation and linking. `make analyze` enables strict
warnings, `-Werror` and GCC's analyzer. Compiler/linker overrides use ordinary
Make variables, for example:

```sh
make release CC=gcc CFLAGS='-std=c99 -Wall -Wextra -Werror'
```

`make clean` removes current debug/release/sanitizer/analysis artifacts and the
root executable; it retains other files under `build/`.

Original coursework checks are optional and require local `input/`, `ref/`,
`tasks/`, `tests.json`, `dummy` and the `image_check` executable:

```sh
make test-assignment
make sanitize
ASAN_OPTIONS=detect_leaks=0:allocator_may_return_null=1 \
  python3 -B tests/run_assignment.py --binary ./build/asan/image_editor
python3 -B tests/run_assignment.py --binary ./build/debug/image_editor \
  --cases 00 04 28 32 --valgrind
```

The adapter runs original command inputs in temporary directories, compares
stdout with `diff -w -B`, and uses the supplied image checker, whose tolerance
is five sample levels. It copies `dummy` and preserves original references and
historical outputs. These are functional results, not an official grade or
style score. Public tests and CI use only self-generated fixtures.

### Recorded results

Local checks on 2026-10-18 used GCC 11.4.0, GNU Make 4.3, Python 3.10.12 and
Valgrind 3.18.1. Logs and reports are local artifacts under `build/p1/` and
`build/stdout-fix/`; they are excluded from source distribution.

| Check | Result |
| --- | --- |
| Independent regressions, debug/release/ASan+UBSan | 36/36 test methods passed in each mode |
| Focused Valgrind regressions | 5/5 passed; no reported memory errors/leaks |
| Original assignment cases, release and ASan+UBSan | 69/69 passed in each mode; 508 image comparisons each |
| Generated P0/P1 comparison | 32 pipelines; stdout and all 128 saved images identical |
| Clean source export | Debug/release/ASan+UBSan tests passed under a path containing spaces |
| Strict warnings and GCC analyzer | No diagnostics |
| README example | Generated and saved the documented 3x3 RGB image |
| Historical file hashes | 1,418 original files unchanged |

Independent and assignment results include the stdout error correction.
Valgrind and P0/P1 comparison results predate that correction.
See [history](P0_IMPLEMENTATION.md) for the earlier verification record and
[measurements](PERFORMANCE.md#functional-comparison) for differential-test usage.

Local sanitizer runs disabled LeakSanitizer because process tracing prevents it
from running. Native/CI defaults enable it; Valgrind separately checks leaks.
The [workflow](../.github/workflows/ci.yml) uses Ubuntu 24.04, read-only repository
permissions and a pinned checkout action. Its commands have passed locally;
hosted Actions and native LeakSanitizer execution remain unverified.
