/*
 * SigprocFile.h
 *
 *  Created on: 27 Oct 2016
 *      Author: ban115
 */

#ifndef SIGPROCFILE_H_
#define SIGPROCFILE_H_

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include <string>
#include <vector>

#include "DataSource.h"

#define MAX_HDR_SIZE 4096

/**
 * Reads and writes SIGPROC filterbank files and merges coarse channels.
 * SIGPROC data is ordered by time, IF, then frequency (frequency varies fastest).
 *
 * @see http://sigproc.sourceforge.net/sigproc.pdf
 */
class SigprocFile : public DataSource {
public:
    static constexpr double default_oversampling_factor = 32.0 / 27.0;
    // Open an existing filterbank, or construct metadata for a new one.
    SigprocFile(const char* filename, int header_size = -1);
    SigprocFile(int nbits, int nifs, int nchans, double fch1, double foff, double tstart,
                double tsamp);
    SigprocFile();
    virtual ~SigprocFile();
    void Close();
    // Locate a keyword's value in the binary header and read it using its expected type.
    const char* header_find(const char* hname) const;
    int header_int(const char* hname) const;
    double header_double(const char* hname) const;
    // Not implemented by this class; the merger reads into its own buffers.
    size_t read_samples(void** output);
    char get_keyword_type(const char* keyword);

    // Read up to size bytes, returning the byte count or -1 on a read error.
    int read(void* buffer, int size);

    int WriteHeader(const char* filename, bool bClose = true, bool bNewFile = false);
    int WriteData(float* buffer, int n_channels);
    // Build a header from the metadata, optionally converting a Unix start time to MJD.
    int FillHeader(bool recalc_tstart = true, bool fill_radec = false);
    // Update an existing keyword or append one at start_index.
    int SetHeaderValue(char* pHeader, int header_len, const char* keyword, double value,
                       int value_int = 0, const char* value_str = NULL, int start_index = -1);
    int SetHeaderValue(const char* keyword, double value);
    int SetHeaderValue(const char* keyword, int value);

    // Merge inputs in list order into 32-bit floats. foff_sign changes the
    // output header's frequency increment, not the order of the input data.
    // avg_spectrum receives per-channel means over complete spectra; if null,
    // it is allocated with new[] and the caller must release it with delete[].
    // A supplied buffer must have room for all output channels.
    // Returns the output channel count on success, or -1 on a merge error.
    //
    // File splitting parameters:
    // max_file_size_bytes: 0 = no limit; >0 = split when file exceeds this size
    // max_duration_seconds: 0.0 = no limit; >0.0 = split when duration exceeds this time
    static int MergeCoarseChannels(std::vector<std::string>& fil_file_list, const char* out_file,
                                   double*& avg_spectrum, int foff_sign = 1,
                                   uint64_t max_file_size_bytes = 0, double max_duration_seconds = 0.0);

    // Removes oversampled overlap channels and optionally trims the output
    // to a multiple of 128, 64, 16, 4 or 2 channels.
    // Same file splitting parameters as MergeCoarseChannels.
    static int MergeOversampledCoarseChannels(std::vector<std::string>& fil_file_list,
                                              const char* out_file, double*& avg_spectrum,
                                              int foff_sign = 1,
                                              bool bFreddaCompatibleOutput = false,
                                              double oversampling_factor = default_oversampling_factor,
                                              uint64_t max_file_size_bytes = 0,
                                              double max_duration_seconds = 0.0);

    int nifs() { return m_nifs; }
    int nbits() { return m_nbits; }
    int nbits(int nbits) {
        m_nbits = nbits;
        return m_nbits;
    }
    int nbeams() { return nifs(); }
    int nchans() { return m_nchans; }
    int npols() { return 1; }
    int nants() { return 1; }
    size_t samples_read() { return m_samples_read; }
    size_t current_sample() { return m_current_sample; }
    double fch1(double _fch1) {
        m_fch1 = _fch1;
        return m_fch1;
    }
    double fch1() { return m_fch1; }
    double foff() { return m_foff; }
    double foff(double _foff) {
        m_foff = _foff;
        return m_foff;
    }
    double tstart() { return m_tstart; }
    double tsamp() { return m_tsamp; }
    DataOrder data_order() { return DataOrder_TFBP; }

    char* name() { return m_filename; }

    char* gethdr() { return m_hdr; }

    int gethdrbytes() { return m_hdr_nbytes; }

    uint64_t getdatabytes() { return m_data_nbytes; }

    uint64_t getfilebytes() { return m_file_nbytes; }

    const char* sourcename() { return m_sourcename.c_str(); }

    void sourcename(const char* src) { m_sourcename = src; }

    void src_raj(double _src_raj) { m_src_raj = _src_raj; }

    double src_raj() { return m_src_raj; }

    void src_dej(double _src_dej) { m_src_dej = _src_dej; }

    double src_dej() { return m_src_dej; }

    void telescope_id(int tele_id) { m_telescope_id = tele_id; }

    int telescope_id() { return m_telescope_id; }

    void rewind() {
        if (m_file) {
            printf("DEBUG : rewind\n");
            ::rewind(m_file);
            m_samples_read = 0;
            seek_sample(0);
        }
    }

    // Seek to a time-sample index and return its byte offset in the file.
    size_t seek_sample(size_t t);

    static bool DoesFileExist(const char* fname);
    static int64_t GetFileSize(const char* filename);

private:
    double m_fch1;
    double m_foff;
    double m_tstart; // MJD for SIGPROC input; may be Unix time for newly constructed files.
    double m_tsamp;
    int m_nifs;
    int m_nchans;
    int m_nbits;
    std::string m_sourcename;
    int m_telescope_id;
    double m_src_raj;
    double m_src_dej;

    size_t m_samples_read;
    size_t m_current_sample;

    FILE* m_file;
    int m_fd;
    char* m_filename;
    char m_hdr[MAX_HDR_SIZE]; // Binary header, including length-prefixed keywords.
    size_t m_hdr_nbytes;
    uint64_t m_file_nbytes;
    uint64_t m_data_nbytes;
};

#endif /* SIGPROCFILE_H_ */
