# merge_coarse_channels

Standalone extraction of `merge_coarse_channels` and its SIGPROC dependencies
from [mwafrb](https://github.com/marcinsokolowski/mwafrb). The small
`msfitslib` utilities used by the original have been replaced with local
equivalents; no `msfitslib`, CFITSIO or libnova installation is required.

## Build

Requires CMake 3.16 or newer and a C++20 compiler targeting Linux or macOS.

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
cmake --install build --prefix /path/to/install
```

Single-configuration generators default to `Release`, using the compiler's
optimized Release flags and link-time optimization when supported. Specify
`-DCMAKE_BUILD_TYPE=Debug` for a debug build. With a multi-configuration
generator, select Release when building and testing:

```sh
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

### Portability versus optimization

Release builds target the compiler's default CPU for portability. To optimize
for the build machine instead, configure with `-DMERGE_NATIVE_OPTIMIZATION=ON`.
The resulting executable may not run on other CPUs. This option applies only
to Release builds and cannot be used when cross-compiling.

### Platform options

CMake detects the target OS by default (`CMAKE_SYSTEM_NAME`). Linux enables
the `posix_fadvise` sequential-read hint by default; macOS disables it.
These settings can be overridden at configure time:

```sh
cmake -S . -B build -DMERGE_TARGET_PLATFORM=macos
cmake -S . -B build -DMERGE_TARGET_PLATFORM=linux -DMERGE_USE_POSIX_FADVISE=OFF
```

`MERGE_TARGET_PLATFORM` accepts `auto` (default), `linux`, or `macos`; it
selects build settings and does not change the compiler's target. For
cross-compilation, provide an appropriate CMake toolchain. The
`MERGE_USE_POSIX_FADVISE` option can be set to `ON` or `OFF`; enabling it
requires the target system to provide `posix_fadvise`. Options are cached,
so use a fresh build directory or explicitly reset them when changing targets.

## Usage

```sh
build/merge_coarse_channels input1.fil,input2.fil output.fil [options]
build/merge_coarse_channels -h
```

The program writes an average spectrum alongside the output, with its
extension replaced by `.avg_spectrum` (if the output has no dot, the original
utility writes `.avg_spectrum` in the working directory). Oversampled mode
(`-o`) also writes `merged_fine_channels.txt` in the working directory.
`-F` enables the original FREDDA channel-count adjustment for oversampled
merges. Without `-o`, the inputs are treated as critically sampled.

With `-o`, `-r FACTOR` sets the oversampling factor as a decimal or a ratio,
such as `-r 1.25` or `-r 5/4`. It must be finite and greater than 1; the
default is 32/27. The existing overlap selection and boundary averaging are
applied using the specified factor. Oversampled inputs must have equal channel
counts.

8-bit and 32-bit inputs can be combined; output samples are 32-bit floats.
Each input must begin with the length-prefixed SIGPROC `HEADER_START` keyword.
Only complete spectra present in every input are written and included in the
average. The program fails if no complete spectra can be merged.

The extracted code is distributed under GPL-3.0; see [LICENSE](LICENSE).

## Developer notes

The integration test requires Python 3; it is registered when Python is
available at CMake configure time.

The C++ sources and tests use the project's `.clang-format` configuration,
which requires clang-format 21 or newer. From the project root, check
formatting with:

```sh
clang-format --dry-run --Werror src/*.cpp src/*.h tests/*.cpp
```

To apply the formatting:

```sh
clang-format -i src/*.cpp src/*.h tests/*.cpp
```

On macOS, use `xcrun clang-format` in place of `clang-format` if it is not
available on your PATH.
