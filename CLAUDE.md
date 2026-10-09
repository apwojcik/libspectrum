# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

libspectrum is a C library for reading/writing ZX Spectrum emulator file
formats: snapshots (.z80, .szx, .sna, .zxs, .sp, .snp, +D), tape images
(.tzx, .tap, .pzx, .csw, .wav, Warajevo, Z80Em, .spc/.sta/.ltp), input
recordings (.rzx), disk images (identification only), Timex cartridges
(.dck), and IDE hard disk images (.hdf). It's a dependency of the Fuse
ZX Spectrum emulator and related tools, so public API/ABI stability
matters (see "ABI/versioning" below).

## Build systems

There are two build systems in this repo:

- **Autotools** (primary, used by CI and releases): `autogen.sh` → `configure` → `make`. Needs `perl` (test fixtures only) and `python3` (`generate.py`/`accessor.py`/`tape_accessors.py`/`tape_set.py` codegen) on `PATH`.
- **CMake** (`CMakeLists.txt`, newer, library-only — no tests): builds the `spectrum` target directly from the checked-in sources, generating `libspectrum.h`/`snap_accessors.c`/`tape_accessors.c`/`tape_set.c` itself via the same Python scripts as Autotools. Use this for quick IDE-driven builds; use Autotools for anything release- or test-related.

### Autotools workflow

```sh
./autogen.sh          # first time only, or after configure.ac/Makefile.am changes
./configure
make
make check             # builds and runs test/test
sudo make install
```

Useful `configure` flags (see `.github/workflows/build_linux_sub.yml` / `build_windows_sub.yml` for the exact combinations CI exercises):

- `--without-zlib`, `--without-bzip2`, `--without-libgcrypt`, `--without-libaudiofile` — disable optional dependencies.
- `--with-fake-glib` — use the internal GLib replacement (`myglib/`) instead of real GLib; this is how Windows/Cygwin and the Wii cross-build are configured, so changes touching glib-backed data structures (hash tables, slists, atomics) need to work under both real and fake glib.

### Running tests

`make check` builds `test/test` and runs the whole suite via automake's `TESTS` runner. To iterate on a subset directly:

```sh
test/test                # run everything
test/test 5               # run test #5 only (1-based index, see output of a full run for numbers)
test/test 5-10             # run a range
test/test tzx              # run every test whose name or description contains "tzx" (substring match)
```

`test/test` statically links against the library archive to reach private helpers directly (see `test_test_DEPENDENCIES` / `BUILD_TEST_ARCHIVE` in `test/Makefile.am`), so it needs a build, not just the installed shared library. Test registration lives in `test/registry.c` (`TEST_ENTRY` macros); add new tests there and implement them in the relevant `test/<area>.c` file.

### ABI compatibility check

Maintainers can run an ABI diff between two refs in Docker (does not touch your working tree — clones the repo into a temp dir inside the container):

```sh
docker build -f tools/Dockerfile.abi -t libspectrum-abi-check .
docker run --rm -v "$PWD:/work" libspectrum-abi-check sh /work/tools/check-abi-in-docker.sh [<old-ref> [<new-ref>]]
```

Report lands in `abi-out/`. See `doc/releasing.md` for full details and when to bump `libspectrum_la_LDFLAGS` (libtool version-info) in `Makefile.am`.

## Architecture

### Generated code — don't hand-edit without updating the source

Several core files are **generated at build time**, driven by plain-text spec files. Edit the spec/template, not the generated output:

- `libspectrum.h` (the public header) is generated from `libspectrum.h.in` by `generate.py`, which also reads `config.h`'s `HAVE_*` defines directly to decide what to emit for stdint typedefs, the glib-replacement shim, gcrypt/zlib/bz2/wav capability macros, and the snap/tape accessor declarations (reading `snap_accessors.txt`/`tape_accessors.txt` itself — see below). This replaced an older two-stage Perl pipeline (`make-perl.c` compiled a probe that emitted Perl, concatenated with `generate.pl.in`, run under `perl -p`); `generate.py` is self-contained and needs no compiler.
- `snap_accessors.c` is generated from `snap_accessors.txt` by `accessor.py`. Each line in the `.txt` declares one field of `libspectrum_snap` (type, name, optional array length, optional default value) and produces a getter/setter pair.
- `tape_accessors.c` / `tape_set.c` are generated from `tape_accessors.txt` by `tape_accessors.py` / `tape_set.py` similarly, for tape block fields.

When adding a new snapshot or tape-block field, add it to the relevant `.txt` file rather than writing accessors by hand — the pattern is declarative, not imperative.

### Module layout

- `libspectrum.c` / `libspectrum.h.in` — top-level init, versioning, shared enums (`libspectrum_error`, `libspectrum_id_t`), format (de)compression dispatch.
- `internals.h` — the only header for routines shared *between* library source files but not exposed to users (format-specific read/write entry points, byte/word helpers, glib-replacement shims). New cross-file internal functions go here, not in the public header.
- Snapshots: `snapshot.c` (format identification/dispatch) + one file per format (`z80` logic lives in `libspectrum.c`'s snapshot paths plus `szx.c`, `sna.c`, `snp.c`, `sp.c`, `dsnap.c`, `zxs.c`, `plusd.c`); `snap_accessors.c` (generated) holds the field accessors for the `libspectrum_snap` opaque struct.
- Tapes: `tape.c` is the large core (block list management, edge/pulse iteration, cursors). Per-format read/write lives in `tap.c`, `tzx_read.c`/`tzx_write.c`, `pzx_read.c`/`pzx_write.c`, `csw.c`/`csw_write.c`, `wav*.c`, `warajevo_read.c`, `z80em.c`. `tap_pzx.c` and `tzx_pzx.c` handle cross-format conversion (e.g. recognising ROM-loader pulse sequences inside PZX/generalised-data blocks so they can round-trip through TAP). `tape_block.c`/`tape_block.h` define the per-block-type structs and per-type state machines used during edge iteration; `tape_accessors.c`/`tape_set.c` (generated) are the accessors for those structs.
- `rzx.c` — input recording format, including the DSA-signed competition mode (`crypto.c` does the signing/verification, backed by libgcrypt when available).
- Disk/cartridge/misc: `ide.c` (.hdf drives), `microdrive.c` (.mdr), `dck.c` (Timex cartridges), `mmc.c`.
- Support layer: `buffer.c` (growable byte buffer used by every writer), `utilities.c`, `memory.c`, `symbol_table.c`, `timings.c` (ms↔tstates conversions, pulse timing constants), `creator.c` (creator-info metadata embedded in output files), `file.c` (load-from-disk + type identification), `zip.c`/`zlib.c`/`bzip2.c` (compressed container support).
- `myglib/` — minimal GLib replacement (hash table, slist, atomics) used when built `--with-fake-glib`, for platforms without real GLib (Windows/Cygwin builds, Wii).

### Public API conventions

- Most structures (`libspectrum_snap`, `libspectrum_tape`, tape blocks, etc.) are opaque; callers go through accessor functions, most of which are generated (see above).
- Nearly every fallible entry point returns `libspectrum_error`, with `LIBSPECTRUM_ERROR_NONE` (0) for success; many take an `int *out_flags`/`in_flags` pair for optional/bitmask behaviour (e.g. snapshot/tape writers).
- Format identification and dispatch goes through `libspectrum_id_t` — read paths typically sniff/accept a type and dispatch internally (`internal_*_read` functions in `internals.h`) rather than exposing per-format entry points publicly.

## Release process

See `doc/releasing.md` for the full maintainer workflow (release metadata via `update-release.py`, ChangeLog updates, ABI check, tag/publish). There's an optional repository skill at `.agents/skills/update-changelog/SKILL.md` for drafting the ChangeLog entry — it drafts first and waits for confirmation before editing the file; follow its formatting rules exactly if invoked (8/10-space bullet indents, attribution format, "Various minor bug fixes/improvements" bucket for small items, etc.).
