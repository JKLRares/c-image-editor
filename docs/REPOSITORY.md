# Source distribution

[tools/source-files.txt](../tools/source-files.txt) lists the source, tests,
scripts, documentation and CI workflow included in an export.

```sh
python3 -B tools/export_source.py --list
python3 -B tools/export_source.py --destination /tmp/image-editor-source
make check-export
```

The destination must be new or empty. The exporter rejects missing or symlinked
source files and copies only the listed paths. The copy builds and tests without
the coursework workspace, Git metadata, Rust or grading assets. `make check-export`
verifies this in a temporary path containing spaces. `make pack` exports the same
set to `build/source-package` and refuses to overwrite a nonempty destination.

## Excluded material

| Material | Reason |
| --- | --- |
| `input/`, `ref/`, `tasks/`, `tests.json`, `dummy` | Coursework fixtures and reference data; publication rights unverified |
| `check`, `check_utils/`, `cs/`, `README-checker.md`, `install.sh` | Third-party grading infrastructure |
| `Cargo.toml`, `Cargo.lock`, `src/main.rs`, `image_check`, `target/` | Separate Rust checker and its build artifacts |
| `output/`, `build/`, logs, objects and executables | Generated or historical results |
| Submission ZIP, Makefile examples and historical image files | Archival coursework material |
| `.aws/`, `.codex/`, `.agents/`, caches and credentials | Local environment data |

The [README](../README.md) covers usage. [Design and testing](P1_IMPLEMENTATION.md)
describes interfaces, failure handling and test commands; [measurements](PERFORMANCE.md)
records benchmark methods and samples. [History](P0_IMPLEMENTATION.md) preserves
attribution and earlier verification results.

## Attribution and publication

Preserve the author attribution in `image_editor.c` and the coursework origin
and AI-assistance disclosure in the README and history. Check course publication
rules and rights to contributed code before publishing. Excluding grading assets
does not grant permission to publish a coursework solution, and no license for
third-party checker materials is implied.

`.gitignore` excludes local root assets; it does not remove tracked files or
history. Review intended files and historical branches before publication.
