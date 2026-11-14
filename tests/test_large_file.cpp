// Include the implementation to exercise the internal merge finalizer directly.
#include "../src/SigprocFile.cpp"

int main(int argc, char** argv) {
    if (argc != 2) {
        fprintf(stderr, "Expected a filterbank filename\n");
        return 1;
    }

    try {
        SigprocFile input(argv[1]);
        printf("File size = %" PRIu64 "\n", input.getfilebytes());
        printf("Data size = %" PRIu64 "\n", input.getdatabytes());
        printf("Stat size = %" PRId64 "\n", SigprocFile::GetFileSize(argv[1]));
    } catch (const InvalidSourceFormat&) {
        return 1;
    }

    const uint64_t spectra = UINT64_C(2147483653);
    double average = static_cast<double>(spectra) * 2.5;
    SigprocFile output;
    if (!finish_merge_output(output, &average, 1, spectra) || average != 2.5) {
        fprintf(stderr, "Large spectrum count produced an incorrect average\n");
        return 1;
    }
    return 0;
}
