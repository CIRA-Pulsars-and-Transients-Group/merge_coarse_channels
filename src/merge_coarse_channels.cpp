#include "SigprocFile.h"
#include "FileSplitter.h"

#include <errno.h>
#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <cmath>
#include <string>
#include <vector>

std::string gInputFilFiles = "in1.fil,in2.fil,in3.fil";
std::string gAvgSpectrumFile = "avg_spectrum.txt";
std::vector<std::string> input_fil_files;
std::string gOutputFilFile = "out.fil";
int gFOFFSign = 1;
bool gOverSampled = false;
bool gTrimChannels = false;
double gOversamplingFactor = SigprocFile::default_oversampling_factor;
uint64_t gMaxOutputFileSize = 0;     // in megabytes, 0 = no limit
double gMaxOutputFileDuration = 0.0; // in seconds, 0.0 = no limit

void usage() {
    printf("Usage: merge_coarse_channels INPUTS [options]\n\n"
           "INPUTS is a comma-separated list of filterbank files in merge order.\n\n"
           "Options:\n"
           "  -O FILE    Output filterbank file (default: out.fil)\n"
           "  -s SIGN    Multiply the output FOFF by SIGN: 1 unchanged, -1 flipped (default: 1)\n"
           "  -o         Merge oversampled coarse channels (default: critically sampled)\n"
           "  -r FACTOR  Oversampling factor as a decimal or ratio; requires -o (default: 32/27)\n"
           "  -F         With -o, trim the output tail channels to the largest applicable\n"
           "             multiple of 128, 64, 16, 4 or 2\n"
           "  -M SIZE    Maximum output file size in megabytes (creates split files)\n"
           "  -D DURATION Maximum output file duration in seconds (creates split files)\n"
           "  -h         Show this help\n\n"
           "A positional output file after INPUTS is also accepted for compatibility.\n");
}

void parse_cmdline(int argc, char* argv[]) {
    char optstring[] = "hs:oFr:O:M:D:";
    int opt;
    opterr = 0;
    bool factor_supplied = false;

    while ((opt = getopt(argc, argv, optstring)) != -1) {
        switch (opt) {
        case 's':
            if (optarg) {
                gFOFFSign = atol(optarg);
            }
            break;

        case 'F':
            gTrimChannels = true;
            break;

        case 'o':
            gOverSampled = true;
            break;

        case 'O':
            if (!optarg[0]) {
                fprintf(stderr, "ERROR : output filename must not be empty\n");
                exit(EXIT_FAILURE);
            }
            gOutputFilFile = optarg;
            break;

        case 'M': {
            char* end = NULL;
            errno = 0;
            double size_mb = strtod(optarg, &end);
            bool valid = end != optarg && errno != ERANGE && std::isfinite(size_mb) && size_mb > 0;
            if (!valid || *end != '\0') {
                fprintf(stderr,
                        "ERROR : maximum file size must be a positive number in megabytes\n");
                exit(EXIT_FAILURE);
            }
            gMaxOutputFileSize = static_cast<uint64_t>(size_mb * 1024.0 * 1024.0);
            break;
        }

        case 'D': {
            char* end = NULL;
            errno = 0;
            double duration = strtod(optarg, &end);
            bool valid =
                end != optarg && errno != ERANGE && std::isfinite(duration) && duration > 0;
            if (!valid || *end != '\0') {
                fprintf(stderr,
                        "ERROR : maximum file duration must be a positive number in seconds\n");
                exit(EXIT_FAILURE);
            }
            gMaxOutputFileDuration = duration;
            break;
        }

        case 'r': {
            factor_supplied = true;
            char* end = NULL;
            errno = 0;
            double numerator = strtod(optarg, &end);
            bool valid = end != optarg && errno != ERANGE && std::isfinite(numerator);
            double denominator = 1.0;
            if (*end == '/') {
                char* denominator_start = end + 1;
                errno = 0;
                denominator = strtod(denominator_start, &end);
                valid = valid && end != denominator_start && errno != ERANGE &&
                        std::isfinite(denominator) && denominator > 0;
            }
            gOversamplingFactor = valid ? numerator / denominator : 0.0;
            if (!valid || *end != '\0' || !std::isfinite(gOversamplingFactor) ||
                gOversamplingFactor <= 1.0) {
                fprintf(stderr, "ERROR : oversampling factor must be finite and greater than 1\n");
                exit(EXIT_FAILURE);
            }
            break;
        }

        case 'h':
            usage();
            exit(EXIT_SUCCESS);

        default:
            fprintf(stderr, "Unknown or incomplete option -%c\n", optopt);
            usage();
            exit(EXIT_FAILURE);
        }
    }

    if (factor_supplied && !gOverSampled) {
        fprintf(stderr, "ERROR : -r requires oversampled mode (-o)\n");
        exit(EXIT_FAILURE);
    }

    // strtok_r modifies its input, so tokenize a writable copy of the file list.
    std::vector<char> input_copy(gInputFilFiles.begin(), gInputFilFiles.end());
    input_copy.push_back('\0');
    char* saveptr = NULL;
    for (char* token = strtok_r(input_copy.data(), ",", &saveptr); token != NULL;
         token = strtok_r(NULL, ",", &saveptr)) {
        std::string value(token);
        size_t first = value.find_first_not_of(' ');
        size_t last = value.find_last_not_of(' ');
        input_fil_files.push_back(
            first == std::string::npos ? "" : value.substr(first, last - first + 1));
    }
}

void print_parameters() {
    printf("PARAMETERS:\n");
    printf("  Input .fil files = %s\n", gInputFilFiles.c_str());
    for (const auto& input : input_fil_files) {
        printf("    %s\n", input.c_str());
    }
    printf("  Output .fil file stub = %s\n", gOutputFilFile.c_str());
    printf("  Average spectrum output file = %s\n", gAvgSpectrumFile.c_str());
    printf("  FOFF sign = %d\n", gFOFFSign);
    printf("  Channels oversampled = %d\n", gOverSampled);
    if (gOverSampled) {
        printf("  Oversampling factor = %.12g\n", gOversamplingFactor);
    }
    printf("  Trim channels = %d\n", gTrimChannels);
    if (gMaxOutputFileSize > 0) {
        printf("  Max output file size = %" PRIu64 " bytes (%.1f MB)\n", gMaxOutputFileSize,
               gMaxOutputFileSize / (1024.0 * 1024.0));
    } else {
        printf("  Max output file size = disabled (no splitting by size)\n");
    }
    if (gMaxOutputFileDuration > 0.0) {
        printf("  Max output file duration = %.2f seconds\n", gMaxOutputFileDuration);
    } else {
        printf("  Max output file duration = disabled (no splitting by duration)\n");
    }
    fflush(stdout);
}

void print_validation_messages() {
    // File splitting configuration
    if (gMaxOutputFileSize > 0 || gMaxOutputFileDuration > 0.0) {
        printf("INFO: File splitting enabled\n");
        if (gMaxOutputFileSize > 0) {
            printf("       Size limit: %.1f MB\n", gMaxOutputFileSize / (1024.0 * 1024.0));
        }
        if (gMaxOutputFileDuration > 0.0) {
            printf("       Duration limit: %.2f seconds (%.2f min)\n", gMaxOutputFileDuration,
                   gMaxOutputFileDuration / 60.0);
        }
    } else {
        printf("INFO: File splitting disabled (single output file)\n");
    }

    // Input file preparation
    printf("INFO: Preparing to merge %zu input file(s)\n", input_fil_files.size());

    for (size_t i = 0; i < input_fil_files.size(); i++) {
        if (input_fil_files[i].empty()) {
            printf("WARNING: Empty filename detected in input list (position %zu)\n", i);
        }
    }

    // Configuration validation warnings/information
    if (input_fil_files.size() == 1) {
        printf("WARNING: Merging single input file (no coarse channels to merge)\n");
    }

    if (gOverSampled) {
        printf("INFO: Oversampled merge mode enabled\n");
        printf("INFO: Oversampling factor: %.12g\n", gOversamplingFactor);
        if (gOversamplingFactor > 2.0) {
            printf("WARNING: High oversampling factor may result in minimal overlap\n");
        }
        if (gOversamplingFactor < 1.01) {
            printf("WARNING: Oversampling factor is very close to 1.0 (minimal improvement)\n");
        }
        if (gTrimChannels) {
            printf("INFO: Output channels will be trimmed to compatible standard sizes\n");
        }
    }

    if (gFOFFSign == -1) {
        printf("INFO: Output frequency offset will be inverted\n");
    }

    fflush(stdout);
}

int main(int argc, char* argv[]) {
    if (argc < 2 || (argc == 2 && strcmp(argv[1], "-h") == 0)) {
        usage();
        exit(0);
    }

    gInputFilFiles = argv[1];
    int options_start = 1;
    if (argc >= 3 && argv[2][0] != '-') {
        gOutputFilFile = argv[2];
        options_start = 2;
    }
    // getopt treats the input or legacy output filename as argv[0].
    parse_cmdline(argc - options_start, argv + options_start);

    // Use the output prefix before the final dot, or an empty prefix if absent.
    size_t dot = gOutputFilFile.find_last_of('.');
    gAvgSpectrumFile =
        (dot == std::string::npos ? "" : gOutputFilFile.substr(0, dot)) + ".avg_spectrum";

    print_parameters();

    print_validation_messages();

    // The selected merge allocates the per-channel averages for the caller.
    double* avg_spectrum = NULL;
    int out_channels = 0;
    if (gOverSampled) {
        out_channels = SigprocFile::MergeOversampledCoarseChannels(
            input_fil_files, gOutputFilFile.c_str(), avg_spectrum, gFOFFSign, gTrimChannels,
            gOversamplingFactor, gMaxOutputFileSize, gMaxOutputFileDuration);
    } else {
        out_channels =
            SigprocFile::MergeCoarseChannels(input_fil_files, gOutputFilFile.c_str(), avg_spectrum,
                                             gFOFFSign, gMaxOutputFileSize, gMaxOutputFileDuration);
    }

    if (out_channels < 0) {
        fprintf(stderr, "ERROR: Merge operation failed\n");
        fprintf(stderr, "ERROR: Unable to process input files\n");
        fprintf(stderr, "INFO: Verify input files are valid filterbank format\n");
        delete[] avg_spectrum;
        return EXIT_FAILURE;
    }

    // Merge completion status
    printf("INFO: Merge operation completed successfully\n");
    printf("INFO: Output channels: %d\n", out_channels);

    FILE* out_f = fopen(gAvgSpectrumFile.c_str(), "w");
    if (!out_f) {
        fprintf(stderr, "ERROR: Cannot create average spectrum file '%s': %s\n",
                gAvgSpectrumFile.c_str(), strerror(errno));
        delete[] avg_spectrum;
        return EXIT_FAILURE;
    }
    // Store one channel index and its mean value per line.
    for (int ch = 0; ch < out_channels; ch++) {
        fprintf(out_f, "%d %.4f\n", ch, avg_spectrum[ch]);
    }
    fclose(out_f);

    // Information about the average spectrum file
    printf("INFO: Average spectrum with %d channels saved to: %s\n", out_channels,
           gAvgSpectrumFile.c_str());

    // Resource cleanup
    printf("INFO: Merge completed successfully\n");
    printf("INFO: Releasing allocated resources\n");

    if (avg_spectrum) {
        delete[] avg_spectrum;
    }
}
