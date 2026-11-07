# Releasing libspectrum

## Overview

This document describes the manual steps used to prepare a libspectrum
release. The `update-release.py` helper updates release metadata in the
main documentation files and in `CMakeLists.txt`, but it does not perform
the full release process by itself.

## Release metadata helper

The repository includes `update-release.py` in the top-level directory.

### Usage

Preview changes:

```sh
./update-release.py --date "12th June, 2026" --version "1.6.2" --dry-run
```

Update the release date only:

```sh
./update-release.py --date "12th June, 2026"
```

Update both release date and version:

```sh
./update-release.py --date "12th June, 2026" --version "1.6.2"
```

### Files updated

When given `--date`, the script updates:

- the `.TH` line in `doc/libspectrum.3`
- the final date line in `README`

When given `--version`, it also updates:

- the top version line in `README`
- the top version line in `doc/libspectrum.txt`
- the `project()` version in `CMakeLists.txt`

### Files not updated

The script does not update:

- `ChangeLog`
- the `LIBSPECTRUM_ABI_CURRENT`/`REVISION`/`AGE` values in `CMakeLists.txt`
- generated files
- release tarballs, tags, or announcements

## Optional agent skill for ChangeLog updates

The repository also includes an optional maintainer skill at
`.agents/skills/update-changelog/SKILL.md`. If your agent environment
supports repository skills, it can use that skill to draft a new
release entry for `ChangeLog` in the existing project format.

The skill is a convenience helper only. It is not required to build
libspectrum or to prepare a release. If it is unavailable, update
`ChangeLog` manually in the existing style.

## Typical release workflow

1. Update `ChangeLog`. If your agent environment supports repository
   skills, use `.agents/skills/update-changelog/SKILL.md` to draft the
   new top-of-file release entry, review it, and then apply it after
   confirmation. Otherwise, update `ChangeLog` manually in the existing
   format.
2. Run `update-release.py` with `--dry-run`.
3. Run it again without `--dry-run` once the changes look correct.
4. Review the resulting diff.
5. Build the project.
6. Run the test suite.
7. Create the release tarball, tag, and publish using the normal
   maintainer process.

## Versioning notes

The project release version in `CMakeLists.txt` (`project(... VERSION ...)`)
is separate from the shared library ABI version, which is set by the
`LIBSPECTRUM_ABI_CURRENT`, `LIBSPECTRUM_ABI_REVISION` and
`LIBSPECTRUM_ABI_AGE` variables in the same file (the old libtool
`-version-info current:revision:age`; the comment above them has the
rules of thumb). Review both during a release.

## ABI compatibility check with Docker

If you prepare releases, you can run the ABI comparison inside Docker
using the helper files in `tools/`.

The helper does not build in your current working directory. Instead, it
clones the repository into a temporary directory inside the container,
compares exported source snapshots from that clone, and removes the
temporary directory before exit. Only `abi-out/` is written back to the
host checkout.

### Build the ABI checker image

From the top-level `libspectrum/` directory, run:

```sh
docker build -f tools/Dockerfile.abi -t libspectrum-abi-check .
```

### Run the ABI comparison

With no arguments, the helper compares the most recent tag against
`HEAD`:

```sh
docker run --rm \
  -v "$PWD:/work" \
  libspectrum-abi-check \
  sh /work/tools/check-abi-in-docker.sh
```

You can also override the refs explicitly. With one argument, it is
compared against `HEAD`:

```sh
docker run --rm \
  -v "$PWD:/work" \
  libspectrum-abi-check \
  sh /work/tools/check-abi-in-docker.sh 1.6.0
```

With two arguments, both old and new refs are explicit:

```sh
docker run --rm \
  -v "$PWD:/work" \
  libspectrum-abi-check \
  sh /work/tools/check-abi-in-docker.sh 1.6.0 HEAD
```

The helper script will:

1. clone the mounted repository into a temporary directory inside the
   container
2. select the old ref and new ref to compare
3. export each ref into a separate source tree without mutating your
   checkout
4. configure both trees with CMake using a stable feature set
5. build and install each tree into a separate prefix
6. run `abi-dumper` on each installed shared library
7. run `abi-compliance-checker` and write an HTML report under
   `abi-out/`

The CMake options used are:

```sh
-DUSE_FAKE_GLIB=ON -DLIBGCRYPT_SUPPORT=OFF -DWAV_SUPPORT=OFF
```

Refs that predate the move to CMake have no `CMakeLists.txt`; for those
the helper falls back to the old Autotools build.

Using a fixed feature set helps keep the exported ABI consistent across
runs and avoids accidental differences caused by optional dependencies.

The Docker image also installs Universal Ctags for `abi-dumper`, and the
helper builds with `CFLAGS='-Og -g'` to improve ABI analysis
and avoid `abi-dumper` warnings about release-style optimisation flags.

By default, this compares committed `HEAD`. Uncommitted local changes in
your working tree are not included.

### Outputs

The run creates these host-visible files under `abi-out/`:

- `libspectrum-<ref>.dump` for each side of the comparison
- `libspectrum-abi-report-<old>-to-<new>.html`

Open the HTML report in a browser and use it to decide whether the
`LIBSPECTRUM_ABI_*` values should change for a backward-compatible ABI update or an ABI break.

## Verification

After updating release metadata, verify with:

```sh
cmake -S . -B build -DLIBSPECTRUM_BUILD_TESTS=ON
cmake --build build
(cd build && ctest)
```

Also inspect the diff manually to confirm the release date and version
were updated in the expected files.

## Caveats

- The date should be passed in the format used by the existing docs, for
  example `12th June, 2026`.
- The script is intended as a maintainer convenience tool.
- Review all changes before creating a release.
