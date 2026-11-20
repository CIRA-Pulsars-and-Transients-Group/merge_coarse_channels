/*
 * test_filesplitter.cpp
 *
 * Tests for FileSplitter class.
 */

#include "FileSplitter.h"

#include <cassert>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

void test_filename_parsing() {
    std::cout << "Testing filename parsing..." << std::endl;

    FileSplitter splitter("output.fil");
    assert(splitter.GetBaseFilename() == "output");
    assert(splitter.GetExtension() == ".fil");

    FileSplitter splitter2("merged");
    assert(splitter2.GetBaseFilename() == "merged");
    assert(splitter2.GetExtension() == "");

    FileSplitter splitter3("path/to/data.filterbank");
    assert(splitter3.GetBaseFilename() == "path/to/data");
    assert(splitter3.GetExtension() == ".filterbank");

    std::cout << "  [PASS] Filename parsing tests passed" << std::endl;
}

void test_filename_generation() {
    std::cout << "Testing filename generation..." << std::endl;

    FileSplitter splitter("output.fil");
    assert(splitter.GetCurrentFilename() == "output_0001.fil");
    assert(splitter.GetNextFilename() == "output_0002.fil");

    splitter.AdvanceToNextFile();
    assert(splitter.GetCurrentFilename() == "output_0002.fil");
    assert(splitter.GetNextFilename() == "output_0003.fil");

    for (int i = 2; i < 100; i++) {
        splitter.AdvanceToNextFile();
    }
    assert(splitter.GetCurrentFilename() == "output_0100.fil");

    std::cout << "  [PASS] Filename generation tests passed" << std::endl;
}

void test_size_threshold() {
    std::cout << "Testing size-based splitting..." << std::endl;

    // Max 1000 bytes per file
    FileSplitter splitter("output.fil", 1000);
    assert(!splitter.NeedsSplit(999, 0.0));
    assert(splitter.NeedsSplit(1000, 0.0));
    assert(splitter.NeedsSplit(1001, 0.0));

    std::cout << "  [PASS] Size threshold tests passed" << std::endl;
}

void test_duration_threshold() {
    std::cout << "Testing duration-based splitting..." << std::endl;

    // Max 100 seconds per file
    FileSplitter splitter("output.fil", 0, 100.0);
    assert(!splitter.NeedsSplit(0, 99.9));
    assert(splitter.NeedsSplit(0, 100.0));
    assert(splitter.NeedsSplit(0, 100.1));

    std::cout << "  [PASS] Duration threshold tests passed" << std::endl;
}

void test_combined_thresholds() {
    std::cout << "Testing combined size and duration thresholds..." << std::endl;

    // Both limits set - either one triggers split
    FileSplitter splitter("output.fil", 5000, 60.0);

    // Below both thresholds
    assert(!splitter.NeedsSplit(4000, 50.0));

    // Size threshold exceeded
    assert(splitter.NeedsSplit(5000, 50.0));
    assert(splitter.NeedsSplit(6000, 50.0));

    // Duration threshold exceeded
    assert(splitter.NeedsSplit(4000, 60.0));
    assert(splitter.NeedsSplit(4000, 70.0));

    // Both exceeded
    assert(splitter.NeedsSplit(5000, 60.0));

    std::cout << "  [PASS] Combined threshold tests passed" << std::endl;
}

void test_no_splitting_enabled() {
    std::cout << "Testing disabled splitting..." << std::endl;

    FileSplitter splitter("output.fil", 0, 0.0);
    assert(!splitter.IsSplittingEnabled());
    assert(!splitter.NeedsSplit(999999999, 999999.9));

    std::cout << "  [PASS] Disabled splitting tests passed" << std::endl;
}

void test_created_files_list() {
    std::cout << "Testing created files tracking..." << std::endl;

    FileSplitter splitter("data.fil");
    std::vector<std::string> files = splitter.GetCreatedFiles();
    assert(files.size() == 1);
    assert(files[0] == "data_0001.fil");

    splitter.AdvanceToNextFile();
    files = splitter.GetCreatedFiles();
    assert(files.size() == 2);
    assert(files[0] == "data_0001.fil");
    assert(files[1] == "data_0002.fil");

    for (int i = 0; i < 8; i++) {
        splitter.AdvanceToNextFile();
    }
    files = splitter.GetCreatedFiles();
    assert(files.size() == 10);
    assert(files[9] == "data_0010.fil");

    std::cout << "  [PASS] Created files tracking tests passed" << std::endl;
}

void test_reset() {
    std::cout << "Testing reset functionality..." << std::endl;

    FileSplitter splitter("output.fil");
    splitter.AdvanceToNextFile();
    splitter.AdvanceToNextFile();
    assert(splitter.GetCurrentSequence() == 3);

    splitter.Reset();
    assert(splitter.GetCurrentSequence() == 1);
    assert(splitter.GetCurrentFilename() == "output_0001.fil");

    std::vector<std::string> files = splitter.GetCreatedFiles();
    assert(files.size() == 1);

    std::cout << "  [PASS] Reset tests passed" << std::endl;
}

int main() {
    std::cout << "Running FileSplitter tests...\n" << std::endl;

    try {
        test_filename_parsing();
        test_filename_generation();
        test_size_threshold();
        test_duration_threshold();
        test_combined_thresholds();
        test_no_splitting_enabled();
        test_created_files_list();
        test_reset();

        std::cout << "\n[PASS] All FileSplitter tests passed!" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "\n[FAIL] Test failed with exception: " << e.what() << std::endl;
        return 1;
    }
}
