# merge_coarse_channels

Standalone extraction of `merge_coarse_channels` and its SIGPROC dependencies
from Marcin Sokolowski's [mwafrb](https://github.com/marcinsokolowski/mwafrb). The small
`msfitslib` utilities used by the original have been replaced with local
equivalents; no `msfitslib`, CFITSIO or libnova installation is required.
The original implementation of a lot of the code here was done by Keith Bannister.

The extracted code is distributed under GPL-3.0; see [LICENSE](LICENSE).

## Build

Requires CMake 3.16 or newer and a C++20 compiler targeting Linux or macOS.

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
cmake --install build --prefix /path/to/install
```

You can change your compiler with the usual `-DCMAKE_CXX_COMPILER=...` flag.
Single-configuration generators default to `Release`, using the compiler's
optimized Release flags and link-time optimization when supported. Specify
`-DCMAKE_BUILD_TYPE=Debug` for a debug build. With a multi-configuration
generator, select Release when building and testing:

```sh
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

### Optimization

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
merge_coarse_channels input1.fil,input2.fil [-O output.fil] [options]
merge_coarse_channels -h
```

### Input and output files

Input files are merged in the order listed. `-O` selects the output filterbank
file; the default is `out.fil`. The legacy positional output filename after
the input list is also accepted; if both are given, `-O` takes precedence.

The program writes an average spectrum alongside the output, with its
extension replaced by `.avg_spectrum` (if the output has no dot, the original
utility writes `.avg_spectrum` in the working directory).

### Splitting output files

Use `-M SIZE` to set a maximum output-file size in megabytes, or `-D SECONDS`
to set a maximum duration per output file. For example:

```sh
merge_coarse_channels input1.fil,input2.fil -O merged.fil -M 500
merge_coarse_channels input1.fil,input2.fil -O merged.fil -D 600
merge_coarse_channels input1.fil,input2.fil -O merged.fil -M 500 -D 600
```

When either limit is set, output is written to sequentially numbered files such
as `merged_0001.fil`, `merged_0002.fil`, and so on. If both limits are set,
the file is split when either limit is reached. Splits occur between spectra,
so the limits are applied at spectrum boundaries. `-M` must be a positive
number and is converted using 1 MB = 1024 × 1024 bytes; `-D` must be a
positive duration in seconds. Without either option, the output is a single
file named by `-O` (or `out.fil` by default).

### Oversampled merges

Use `-o` to merge oversampled coarse channels. Without it, inputs are treated
as critically sampled. Oversampled mode also writes
`merged_fine_channels.txt` in the working directory.

For oversampled merges, `-F` trims channels from the end of the merged
channel order to make the output count a multiple of 128, 64, 16, 4 or 2,
preferring the largest applicable divisor. Already compatible counts are
unchanged; a count below 2 cannot be adjusted. For example, 175 channels
become 128. The output header, average spectrum, and `merged_fine_channels.txt`
reflect the resulting count. Without `-o`, `-F` has no effect. This adjustment
focuses on channel count divisibility and does not enforce all compatibility standards.

With `-o`, `-r FACTOR` sets the oversampling factor as a decimal or a ratio,
such as `-r 1.25` or `-r 5/4`. It must be finite and greater than 1; the
default is 32/27. The existing overlap selection and boundary averaging are
applied using the specified factor. Oversampled inputs must have equal channel
counts.

### Supported input data

8-bit and 32-bit inputs can be combined; output samples are 32-bit floats.
Each input must begin with the length-prefixed SIGPROC `HEADER_START` keyword.
Only complete spectra present in every input are written and included in the
average. The program fails if no complete spectra can be merged.

## Developer notes

The integration test requires Python 3; it is registered when Python is
available at CMake configure time.

The C++ sources and tests use the project's `.clang-format` configuration,
which requires clang-format 20 or newer. From the project root, check
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
