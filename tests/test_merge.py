import pathlib
import struct
import subprocess
import sys
import tempfile
import unittest


EXECUTABLE = pathlib.Path(sys.argv.pop(1)).resolve()
LARGE_FILE_PROBE = pathlib.Path(sys.argv.pop(1)).resolve()


def keyword(name):
    encoded = name.encode("ascii")
    return struct.pack("=i", len(encoded)) + encoded


def write_fil(path, nbits, spectra):
    header = keyword("HEADER_START")
    for name, fmt, value in (
        ("nchans", "i", len(spectra[0])),
        ("nifs", "i", 1),
        ("fch1", "d", 100.0),
        ("foff", "d", -0.5),
        ("tstart", "d", 60000.0),
        ("tsamp", "d", 0.001),
        ("nbits", "i", nbits),
        ("src_raj", "d", 0.0),
        ("src_dej", "d", 0.0),
        ("telescope_id", "i", 1),
    ):
        header += keyword(name) + struct.pack("=" + fmt, value)
    header += keyword("HEADER_END")
    fmt = "B" if nbits == 8 else "f"
    values = [value for spectrum in spectra for value in spectrum]
    path.write_bytes(header + struct.pack("=" + fmt * len(values), *values))


def read_fil(path):
    data = path.read_bytes()
    offset = 0
    header = {}
    types = {
        "nchans": "i", "nifs": "i", "nbits": "i", "telescope_id": "i",
        "fch1": "d", "foff": "d", "tstart": "d", "tsamp": "d",
        "src_raj": "d", "src_dej": "d",
    }
    while True:
        length = struct.unpack_from("=i", data, offset)[0]
        offset += 4
        name = data[offset:offset + length].decode("ascii")
        offset += length
        if name == "HEADER_END":
            break
        if name != "HEADER_START":
            fmt = types[name]
            header[name] = struct.unpack_from("=" + fmt, data, offset)[0]
            offset += struct.calcsize(fmt)
    samples = struct.unpack("=" + "f" * ((len(data) - offset) // 4), data[offset:])
    return header, samples


class MergeIntegration(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.directory = pathlib.Path(self.tmp.name)

    def run_merge(self, spectra, nbits=32, options=()):
        paths = []
        for index, samples in enumerate(spectra):
            path = self.directory / f"input{index}.fil"
            write_fil(path, nbits, samples)
            paths.append(str(path))
        output = self.directory / "result.fil"
        subprocess.run(
            [str(EXECUTABLE), " , ".join(paths), str(output), *options],
            cwd=self.directory, check=True, capture_output=True, text=True,
        )
        return read_fil(output)

    def test_normal_merge_float_and_byte(self):
        spectra = [[[1, 2], [3, 4]], [[5, 6], [7, 8]]]
        for nbits in (8, 32):
            with self.subTest(nbits=nbits):
                header, samples = self.run_merge(spectra, nbits, ("-s", "-1"))
                self.assertEqual(header["nchans"], 4)
                self.assertEqual(header["nbits"], 32)
                self.assertEqual(header["foff"], 0.5)
                self.assertEqual(header["tstart"], 60000.0)
                self.assertEqual(samples, (1, 2, 5, 6, 3, 4, 7, 8))
                lines = (self.directory / "result.avg_spectrum").read_text().splitlines()
                self.assertEqual(lines, ["0 2.0000", "1 3.0000", "2 6.0000", "3 7.0000"])

    def test_8_bit_oversampled_matches_float(self):
        spectra = [
            [[index * 50 + ch + 10 * time for ch in range(64)] for time in range(2)]
            for index in range(3)
        ]
        for options in (("-o",), ("-o", "-F")):
            with self.subTest(options=options):
                header32, samples32 = self.run_merge(spectra, 32, options)
                average32 = (self.directory / "result.avg_spectrum").read_text()
                header8, samples8 = self.run_merge(spectra, 8, options)
                average8 = (self.directory / "result.avg_spectrum").read_text()
                self.assertEqual(header8["nchans"], header32["nchans"])
                self.assertEqual(len(samples8), 2 * header8["nchans"])
                self.assertEqual(samples8, samples32)
                self.assertEqual(average8, average32)

    def test_incomplete_spectra_do_not_change_average(self):
        for nbits in (8, 32):
            for options in ((), ("-o",)):
                with self.subTest(nbits=nbits, options=options):
                    spectra = [
                        [[index * 50 + ch + 10 * time for ch in range(64)]
                         for time in range(2 if index != 1 else 1)]
                        for index in range(3)
                    ]
                    header, samples = self.run_merge(spectra, nbits, options)
                    self.assertEqual(len(samples), header["nchans"])
                    lines = (self.directory / "result.avg_spectrum").read_text().splitlines()
                    self.assertEqual(
                        lines, [f"{index} {value:.4f}" for index, value in enumerate(samples)]
                    )

    def test_mixed_sample_sizes(self):
        spectra = [
            [[index * 50 + ch + 10 * time for ch in range(64)] for time in range(2)]
            for index in range(3)
        ]
        paths = []
        for index, values in enumerate(spectra):
            path = self.directory / f"mixed{index}.fil"
            write_fil(path, 8 if index != 1 else 32, values)
            paths.append(str(path))
        for options in ((), ("-o",)):
            with self.subTest(options=options):
                header, expected = self.run_merge(spectra, 32, options)
                expected_average = (self.directory / "result.avg_spectrum").read_text()
                output = self.directory / "mixed.fil"
                subprocess.run(
                    [str(EXECUTABLE), ",".join(paths), str(output), *options],
                    cwd=self.directory, check=True, capture_output=True, text=True,
                )
                mixed_header, mixed = read_fil(output)
                self.assertEqual(mixed_header["nchans"], header["nchans"])
                self.assertEqual(mixed, expected)
                self.assertEqual((self.directory / "mixed.avg_spectrum").read_text(), expected_average)

    def test_no_complete_spectra_is_an_error(self):
        paths = []
        for index in range(3):
            path = self.directory / f"empty{index}.fil"
            write_fil(path, 8, [[1] * 64])
            data = path.read_bytes()
            header_end = data.index(b"HEADER_END") + len(b"HEADER_END")
            path.write_bytes(data[:header_end])
            paths.append(str(path))
        for options in ((), ("-o",)):
            with self.subTest(options=options):
                output = self.directory / "empty-result.fil"
                result = subprocess.run(
                    [str(EXECUTABLE), ",".join(paths), str(output), *options],
                    cwd=self.directory, capture_output=True, text=True,
                )
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("no complete spectra", result.stderr)
                self.assertFalse((self.directory / "empty-result.avg_spectrum").exists())

    def test_invalid_header_start_is_rejected(self):
        source = self.directory / "invalid-start.fil"
        write_fil(source, 32, [[1, 2]])
        valid = source.read_bytes()
        invalid_headers = {
            "wrong keyword": struct.pack("=i", 12) + b"NOT_A_HEADER" + valid[16:],
            "wrong length": struct.pack("=i", 11) + valid[4:],
            "not first": b"\0" + valid,
            "truncated length": valid[:3],
            "truncated keyword": valid[:10],
        }
        for case, data in invalid_headers.items():
            with self.subTest(case=case):
                source.write_bytes(data)
                result = subprocess.run(
                    [str(LARGE_FILE_PROBE), str(source)],
                    capture_output=True, text=True,
                )
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("does not start with HEADER_START", result.stderr)

    def test_oversampled_and_fredda(self):
        spectra = [[[index * 100 + ch for ch in range(64)]] for index in range(3)]
        header, samples = self.run_merge(spectra, options=("-o", "-F"))
        expected = list(range(60))
        expected[-1] = (59 + 104) / 2
        expected += list(range(104, 159))
        expected[-1] = (158 + 204) / 2
        expected += list(range(204, 217))
        self.assertEqual(header["nchans"], 128)
        self.assertEqual(samples, tuple(expected))
        self.assertEqual((self.directory / "merged_fine_channels.txt").read_text(), "128\n")

    def test_oversampled_without_fredda_multiple_spectra(self):
        spectra = [
            [[index * 100 + ch + 10 * time for ch in range(64)] for time in range(2)]
            for index in range(3)
        ]
        header, samples = self.run_merge(spectra, options=("-o",))
        expected = []
        for time in range(2):
            left, middle, right = (file[time] for file in spectra)
            merged = left[:60]
            merged[-1] = (merged[-1] + middle[4]) / 2
            merged += middle[4:59]
            merged[-1] = (merged[-1] + right[4]) / 2
            merged += right[4:]
            expected.extend(merged)
        self.assertEqual(header["nchans"], 175)
        self.assertEqual(samples, tuple(expected))
        averages = (self.directory / "result.avg_spectrum").read_text().splitlines()
        self.assertEqual(len(averages), 175)
        self.assertEqual(averages[0], f"0 {(expected[0] + expected[175]) / 2:.4f}")

    def test_configurable_oversampling_factor(self):
        spectra = [[[index * 100 + ch for ch in range(64)]] for index in range(3)]
        expected = list(range(56))
        expected[-1] = (55 + 108) / 2
        expected += list(range(108, 155))
        expected[-1] = (154 + 208) / 2
        expected += list(range(208, 264))
        for factor in ("4/3", str(4 / 3)):
            with self.subTest(factor=factor):
                header, samples = self.run_merge(spectra, options=("-o", "-r", factor))
                self.assertEqual(header["nchans"], 159)
                self.assertEqual(samples, tuple(expected))
                averages = (self.directory / "result.avg_spectrum").read_text().splitlines()
                self.assertEqual(averages, [f"{i} {v:.4f}" for i, v in enumerate(expected)])

        default_header, default_samples = self.run_merge(spectra, options=("-o",))
        explicit_header, explicit_samples = self.run_merge(
            spectra, options=("-o", "-r", "32/27")
        )
        self.assertEqual(default_header["nchans"], explicit_header["nchans"])
        self.assertEqual(default_samples, explicit_samples)

    def test_fredda_trims_across_coarse_channel_boundaries(self):
        spectra = [[[index * 100 + ch for ch in range(64)]] for index in range(100)]
        full_header, full_samples = self.run_merge(spectra, options=("-o", "-r", "100"))
        header, samples = self.run_merge(spectra, options=("-o", "-r", "100", "-F"))
        self.assertEqual(full_header["nchans"], 164)
        self.assertEqual(header["nchans"], 128)
        self.assertEqual(samples, full_samples[:128])
        averages = (self.directory / "result.avg_spectrum").read_text().splitlines()
        self.assertEqual(averages, [f"{i} {v:.4f}" for i, v in enumerate(samples)])

    def test_single_oversampled_input(self):
        spectra = [[[ch for ch in range(64)]]]
        header, samples = self.run_merge(spectra, options=("-o", "-r", "5/4"))
        self.assertEqual(header["nchans"], 64)
        self.assertEqual(samples, tuple(range(64)))

    def test_invalid_oversampling_factors(self):
        output = self.directory / "rejected.fil"
        for factor in ("", "1", "0", "-2", "nan", "inf", "1/0", "1/-2",
                       "1/2", "2/1/1", "2junk", "1e9999"):
            with self.subTest(factor=factor):
                result = subprocess.run(
                    [str(EXECUTABLE), "input.fil", str(output), "-o", "-r", factor],
                    cwd=self.directory, capture_output=True, text=True,
                )
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("oversampling factor", result.stderr)
                self.assertFalse(output.exists())
        result = subprocess.run(
            [str(EXECUTABLE), "input.fil", str(output), "-r", "5/4"],
            cwd=self.directory, capture_output=True, text=True,
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("-r requires", result.stderr)
        self.assertFalse(output.exists())

    def test_oversampled_inputs_must_have_equal_channel_counts(self):
        paths = []
        for index, count in enumerate((64, 32)):
            path = self.directory / f"unequal{index}.fil"
            write_fil(path, 32, [[0] * count])
            paths.append(str(path))
        output = self.directory / "unequal-result.fil"
        result = subprocess.run(
            [str(EXECUTABLE), ",".join(paths), str(output), "-o", "-r", "5/4"],
            cwd=self.directory, capture_output=True, text=True,
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("channels (first one has", result.stderr)
        self.assertFalse(output.exists())

    def test_removed_special_options_fail(self):
        help_result = subprocess.run(
            [str(EXECUTABLE), "-h"], capture_output=True, text=True, check=True,
        )
        self.assertNotIn("-S", help_result.stdout)
        self.assertNotIn("-C", help_result.stdout)
        for options in (("-o", "-S"), ("-o", "-C", "230")):
            with self.subTest(options=options):
                output = self.directory / "rejected.fil"
                result = subprocess.run(
                    [str(EXECUTABLE), "input.fil", str(output), *options],
                    cwd=self.directory, capture_output=True, text=True,
                )
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(f"option {options[1]}", result.stderr)
                self.assertFalse(output.exists())

    def test_output_without_extension(self):
        source = self.directory / "input.fil"
        write_fil(source, 32, [[1, 2]])
        subprocess.run(
            [str(EXECUTABLE), str(source), "output"],
            cwd=self.directory, check=True, capture_output=True, text=True,
        )
        self.assertTrue((self.directory / ".avg_spectrum").is_file())

    def test_consecutive_input_separators_are_ignored(self):
        first = self.directory / "first.fil"
        second = self.directory / "second.fil"
        write_fil(first, 32, [[1, 2]])
        write_fil(second, 32, [[3, 4]])
        output = self.directory / "result.fil"
        subprocess.run(
            [str(EXECUTABLE), f",{first},,,{second},", str(output)],
            cwd=self.directory, check=True, capture_output=True, text=True,
        )
        self.assertEqual(read_fil(output)[1], (1, 2, 3, 4))

    def test_large_file_sizes_and_spectrum_count(self):
        source = self.directory / "large.fil"
        write_fil(source, 32, [list(range(64))])
        data = source.read_bytes()
        header_bytes = data.index(b"HEADER_END") + len(b"HEADER_END")
        file_bytes = (1 << 32) + 4096
        with source.open("r+b") as file:
            file.truncate(file_bytes)

        result = subprocess.run(
            [str(LARGE_FILE_PROBE), str(source)],
            capture_output=True, text=True, check=True,
        )
        self.assertIn(f"File size = {file_bytes}\n", result.stdout)
        self.assertIn(f"Data size = {file_bytes - header_bytes}\n", result.stdout)
        self.assertIn(f"Stat size = {file_bytes}\n", result.stdout)
        self.assertIn("Spectra count = 2147483653\n", result.stdout)

        others = []
        for index in (1, 2):
            path = self.directory / f"short{index}.fil"
            write_fil(path, 32, [[index * 100 + ch for ch in range(64)]])
            others.append(path)
        for options in ((), ("-o",)):
            with self.subTest(options=options):
                output = self.directory / "large-result.fil"
                merge = subprocess.run(
                    [str(EXECUTABLE), ",".join(map(str, (source, *others))), str(output), *options],
                    cwd=self.directory, capture_output=True, text=True, check=True, timeout=30,
                )
                header, samples = read_fil(output)
                self.assertEqual(len(samples), header["nchans"])
                self.assertEqual(samples[:5], (0, 1, 2, 3, 4))
                self.assertIn("Spectra count = 1\n", merge.stdout)

    def test_file_size_error_is_reported(self):
        result = subprocess.run(
            [str(LARGE_FILE_PROBE), str(self.directory / "missing.fil")],
            capture_output=True, text=True,
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("cannot determine file size", result.stderr)


if __name__ == "__main__":
    unittest.main()
