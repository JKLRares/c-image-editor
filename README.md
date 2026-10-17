# CONSTANTINESCU RARES
# NetPBM Image Editor

A command-line image editor in C for grayscale and RGB NetPBM images.

- P2/P5 grayscale and P3/P6 RGB, with maximum sample values from 1 to 255.
- Rectangle selection, cropping and rotations in multiples of 90 degrees.
- RGB sharpening, edge detection, box blur and Gaussian blur.
- Grayscale histograms and cumulative-distribution equalization.
- Validated input and saving through atomic file replacement.

## Build and run

Requires Linux, GCC and GNU Make. The editor uses C99 and POSIX.1-2008 APIs;
it has no external image-library dependency. Tests and development scripts use
Python 3's standard library. Valgrind is optional.

```sh
make                 # debug build: ./image_editor
./image_editor       # reads commands from standard input
make release         # ./build/release/image_editor
make sanitize        # ./build/asan/image_editor
```

Example:

```sh
cat > sample.ppm <<'PNM'
P3
3 3
255
255 0 0  0 0 0        0 0 255
0 0 0    255 255 255  0 0 0
0 255 0  0 0 0        255 255 0
PNM
./image_editor <<'COMMANDS'
LOAD sample.ppm
APPLY BLUR
ROTATE 90
SAVE result.ppm
EXIT
COMMANDS
```

## Commands

Commands and parameters are case-sensitive, with one command per line.
Filenames cannot contain whitespace; quoting is unsupported. Lines may contain
up to 255 bytes before newline. Oversized or NUL-containing lines are rejected.
EOF releases the loaded image.

| Command | Meaning |
| --- | --- |
| `LOAD filename` | Load an image and select it entirely |
| `SAVE filename [ascii]` | Save binary by default; `ascii` selects P2/P3 |
| `SELECT x1 y1 x2 y2` | Select a rectangle; reversed endpoints are normalized |
| `SELECT ALL` | Select the entire image |
| `CROP` | Keep the selection and select the resulting image entirely |
| `ROTATE angle` | Positive angles rotate clockwise; negative angles counterclockwise |
| `APPLY SHARPEN` | Apply the 3x3 sharpening kernel to selected RGB pixels |
| `APPLY EDGE` | Apply the 3x3 edge-detection kernel |
| `APPLY BLUR` | Apply a 3x3 box average |
| `APPLY GAUSSIAN_BLUR` | Apply the 3x3 weighted Gaussian kernel |
| `HISTOGRAM stars bins` | Grayscale histogram; nonnegative star limit, 2–256 power-of-two bins |
| `EQUALIZE` | Equalize the entire grayscale image |
| `EXIT` | Release resources and stop |

Coordinates are half-open: `[x1,x2) × [y1,y2)`. Partial selections must be square
unless the angle is a multiple of 360 degrees. Full rectangular images rotate
normally. Filters preserve the outer image border and read unchanged neighbors,
including pixels outside the selection.

Histogram and equalization operate on the entire image. Equalization uses
`round(Maxval × count(samples <= value) / pixel_count)`, so a constant image,
including black, maps to Maxval. Histograms divide `[0,Maxval]` proportionally;
some bins may be empty.

Invalid commands leave image data unchanged. A syntactically valid `LOAD` that
fails discards the previous image, following the coursework command protocol.
Other failed edits preserve image data and selection. Recoverable errors print
a diagnostic and accept further commands; stdin/stdout errors cause a nonzero
exit status.

## Formats and limitations

P1/P4 bitmaps and 16-bit samples are unsupported. The editor processes stored
sample values without gamma or color-space conversion. Arbitrary-angle rotation
and undo are not implemented. Plain output uses one image row per text line.

Headers support comments and checked dimensions/sample ranges; incomplete rasters
are rejected. In raw P5/P6, exactly one whitespace byte terminates Maxval. Every
following byte belongs to the raster, including whitespace and `#`. A CRLF pair
after Maxval therefore makes LF the first sample. Plain files allow only comments
or whitespace after the raster; raw files load their first image.

SAVE writes a temporary file in the destination directory, flushes and syncs it,
then renames it over the destination. Successful replacement is atomic on a local
POSIX filesystem. Existing final symlinks and nonregular files are rejected.
The parent directory is not synced, so this does not guarantee full crash
durability. See [design details](docs/P1_IMPLEMENTATION.md#saving) for permissions,
metadata, cleanup and concurrency limits.

## Architecture

`image_editor.c` calls the command loop in `editor/commands.c`. The modules are:

| Module | Responsibility |
| --- | --- |
| `image` | Buffer ownership, checked allocation sizes and selection bounds |
| `parse` | Complete decimal-token conversion with range checks |
| `netpbm` | Decoding, encoding and file replacement |
| `operations` | Transformations and histogram calculation |
| `commands` | Input validation, session state and terminal output |

Images use contiguous buffers with explicit ownership. Full 180° rotations swap
pixels in place; filters use scratch space for the selected interior. See
[design and testing](docs/P1_IMPLEMENTATION.md) and
[operation measurements](docs/PERFORMANCE.md).

## Tests

```sh
make -j2 test
make -j2 test-release
make -j2 test-sanitize
make test-valgrind             # requires Valgrind
make analyze                  # strict warnings and GCC static analysis
make check-export             # builds/tests an isolated source-only copy
```

Tests generate their own images, compare exact pixels and command output, and
inject allocation and I/O failures. Builds and independent tests need no grading
assets, Rust installation or network access. Debug, release and sanitizer builds
use separate object directories and generated header dependencies.

The recorded checks include 36 test methods passing in each build mode and
69 original assignment cases passing in release and ASan/UBSan builds. Full
results, optional assignment checks and reproduction commands are in
[design and testing](docs/P1_IMPLEMENTATION.md#testing).

Leak detection is enabled by default. In environments where process tracing
prevents LeakSanitizer from running, use:

```sh
make test-sanitize ASAN_OPTIONS=detect_leaks=0:allocator_may_return_null=1
```

This runs ASan/UBSan without leak detection; Valgrind checks leaks separately.
The GitHub Actions workflow runs independent tests, sanitizers, analysis and the
source-export check. Hosted CI has not yet been verified.