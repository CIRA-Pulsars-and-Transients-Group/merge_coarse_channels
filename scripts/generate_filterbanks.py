#!/usr/bin/env python3
"""Generate contiguous synthetic filterbank files."""

import argparse
import decimal
import math
import pathlib
import random
import struct
import sys


def positive_int(value):
    try:
        number = int(value)
    except ValueError:
        raise argparse.ArgumentTypeError("must be an integer")
    if not 0 < number <= 2147483647:
        raise argparse.ArgumentTypeError("must be between 1 and 2147483647")
    return number


def positive_decimal(value):
    try:
        number = decimal.Decimal(value)
    except decimal.InvalidOperation:
        raise argparse.ArgumentTypeError("must be a positive finite number")
    if not number.is_finite() or number <= 0:
        raise argparse.ArgumentTypeError("must be a positive finite number")
    return number


def finite_float(value):
    try:
        number = float(value)
    except ValueError:
        raise argparse.ArgumentTypeError("must be a finite number")
    if not math.isfinite(number):
        raise argparse.ArgumentTypeError("must be a finite number")
    return number


def keyword(name):
    encoded = name.encode("ascii")
    return struct.pack("=i", len(encoded)) + encoded


def write_fil(path, index, args, spectra):
    header = keyword("HEADER_START")
    for name, fmt, value in (
        ("nchans", "i", args.channels_per_file),
        ("nifs", "i", 1),
        ("fch1", "d", args.fch1 + index * args.channels_per_file * args.foff),
        ("foff", "d", args.foff),
        ("tstart", "d", 60000.0),
        ("tsamp", "d", float(args.tsamp)),
        ("nbits", "i", args.nbits),
        ("src_raj", "d", 0.0),
        ("src_dej", "d", 0.0),
        ("telescope_id", "i", 1),
    ):
        header += keyword(name) + struct.pack("=" + fmt, value)
    header += keyword("HEADER_END")
    rng = random.Random(args.seed + index)
    with path.open("xb") as output:
        output.write(header)
        for _ in range(spectra):
            if args.nbits == 8:
                output.write(
                    rng.getrandbits(8 * args.channels_per_file)
                    .to_bytes(args.channels_per_file, "little")
                )
            else:
                output.write(struct.pack(
                    f"={args.channels_per_file}f",
                    *(rng.random() for _ in range(args.channels_per_file)),
                ))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--output-dir", type=pathlib.Path, default=pathlib.Path(__file__).resolve().parent,
        help="existing output directory (default: directory containing this script)",
    )
    parser.add_argument(
        "--tsamp", type=positive_decimal, default=decimal.Decimal("0.002"),
        help="time resolution in seconds (default: 0.002)",
    )
    parser.add_argument(
        "--duration", type=positive_decimal, default=decimal.Decimal("120"),
        help="duration in seconds; must be an exact multiple of tsamp (default: 120)",
    )
    parser.add_argument(
        "--channels-per-file", type=positive_int, default=64,
        help="fine channels in each file (default: 64)",
    )
    parser.add_argument(
        "--coarse-channels", type=positive_int, default=4,
        help="number of contiguous files (default: 4)",
    )
    parser.add_argument(
        "--nbits", type=int, choices=(8, 32), default=8,
        help="sample format: 8-bit unsigned or 32-bit float (default: 8)",
    )
    parser.add_argument(
        "--fch1", type=finite_float, default=200.0,
        help="first channel frequency in MHz (default: 200)",
    )
    parser.add_argument(
        "--foff", type=finite_float, default=-0.5,
        help="signed fine-channel spacing in MHz (default: -0.5)",
    )
    parser.add_argument("--seed", type=int, default=12345, help="random seed (default: 12345)")
    args = parser.parse_args()
    if not args.output_dir.is_dir():
        parser.error("--output-dir must be an existing directory")
    if args.foff == 0:
        parser.error("--foff must be nonzero")
    tsamp = float(args.tsamp)
    if not math.isfinite(tsamp) or tsamp == 0:
        parser.error("--tsamp must be representable as a positive finite double")
    duration_num, duration_den = args.duration.as_integer_ratio()
    tsamp_num, tsamp_den = args.tsamp.as_integer_ratio()
    spectra, remainder = divmod(duration_num * tsamp_den, duration_den * tsamp_num)
    if remainder:
        parser.error("--duration must be an exact multiple of --tsamp")
    last_fch1 = args.fch1 + (args.coarse_channels - 1) * args.channels_per_file * args.foff
    last_channel = args.fch1 + (args.coarse_channels * args.channels_per_file - 1) * args.foff
    if not math.isfinite(last_fch1) or not math.isfinite(last_channel):
        parser.error("channel frequencies must be finite")
    paths = [args.output_dir / f"filterbank{index}.fil" for index in range(args.coarse_channels)]
    for path in paths:
        if path.exists():
            parser.error(f"output already exists: {path}")
    for index, path in enumerate(paths):
        write_fil(path, index, args, spectra)
        print(path.resolve())


if __name__ == "__main__":
    try:
        main()
    except OSError as error:
        print(f"ERROR: {error}", file=sys.stderr)
        sys.exit(1)
