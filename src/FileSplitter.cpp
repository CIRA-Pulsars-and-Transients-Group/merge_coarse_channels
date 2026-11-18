/*
 * FileSplitter.cpp
 *
 * Implementation of FileSplitter class for managing output file rotation.
 */

#include "FileSplitter.h"

#include <iomanip>
#include <sstream>

FileSplitter::FileSplitter(const std::string& base_filename, uint64_t max_file_size_bytes,
                           double max_duration_seconds)
    : m_base_filename(base_filename), m_max_file_size(max_file_size_bytes),
      m_max_duration(max_duration_seconds), m_current_sequence(1) {
    ParseFilename(base_filename, m_base_filename_no_ext, m_extension);
}

FileSplitter::~FileSplitter() = default;

void FileSplitter::ParseFilename(const std::string& filename, std::string& base_out,
                                 std::string& ext_out) {
    size_t dot_pos = filename.find_last_of('.');

    if (dot_pos != std::string::npos) {
        base_out = filename.substr(0, dot_pos);
        ext_out = filename.substr(dot_pos);
    } else {
        base_out = filename;
        ext_out = "";
    }
}

std::string FileSplitter::GenerateFilename(int sequence) const {
    std::ostringstream oss;
    oss << m_base_filename_no_ext << "_" << std::setfill('0') << std::setw(4) << sequence
        << m_extension;
    return oss.str();
}

bool FileSplitter::NeedsSplit(uint64_t current_bytes_written, double current_duration) const {
    if (m_max_file_size > 0 && current_bytes_written >= m_max_file_size) {
        return true;
    }
    if (m_max_duration > 0.0 && current_duration >= m_max_duration) {
        return true;
    }
    return false;
}

std::string FileSplitter::GetCurrentFilename() const {
    return GenerateFilename(m_current_sequence);
}

std::string FileSplitter::GetNextFilename() const {
    return GenerateFilename(m_current_sequence + 1);
}

void FileSplitter::AdvanceToNextFile() {
    m_current_sequence++;
    // Track that we've generated this filename
    m_created_files.push_back(GenerateFilename(m_current_sequence));
}

std::vector<std::string> FileSplitter::GetCreatedFiles() const {
    std::vector<std::string> all_files;
    for (int i = 1; i <= m_current_sequence; i++) {
        all_files.push_back(GenerateFilename(i));
    }
    return all_files;
}

void FileSplitter::Reset() {
    m_current_sequence = 1;
    m_created_files.clear();
}

bool FileSplitter::IsSplittingEnabled() const {
    return m_max_file_size > 0 || m_max_duration > 0.0;
}
