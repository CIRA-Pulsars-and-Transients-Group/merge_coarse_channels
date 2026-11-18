/*
 * SigprocFile.cpp
 *
 *  Created on: 27 Oct 2016
 *      Author: ban115
 */

#include "SigprocFile.h"
#include "InvalidSourceFormat.h"
#include "FileSplitter.h"

#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#ifdef HAVE_POSIX_FADVISE
#include <fcntl.h>
#endif

int gDebugLevel = 0;

/* Same as strstr but goes through *all* the string - even if it contains nulls
 *
 */
char* mystrnstr(const char* s1, const char* s2, size_t n) {
    size_t s2_len = strlen(s2);
    for (size_t i = 0; i < n - s2_len; i++) {
        if (strncmp(s1 + i, s2, s2_len) == 0) {
            return (char*)(s1 + i);
        }
    }

    return NULL;
}

SigprocFile::SigprocFile()
    : m_file(NULL), m_filename(NULL), m_samples_read(0), m_fd(0), m_nifs(0), m_nchans(0),
      m_nbits(0), m_src_raj(0.00), m_src_dej(0.00) {}

static char* find_string_in_header(char* header, int header_len, const char* template_str) {
    int start = 0;
    int template_len = strlen(template_str);

    while (start < (header_len - template_len)) {
        int match = 1;
        for (int i = 0; i < template_len; i++) {
            if (header[start + i] != template_str[i]) {
                if (gDebugLevel > 0) {
                    printf("start = %d stopped at %c != %c\n", start, header[start + i],
                           template_str[i]);
                }
                match = 0;
                break;
            }
        }

        if (match) {
            if (gDebugLevel > 0) {
                printf("FOUND !!!\n");
            }
            return (header + start);
        }

        start++;
    }

    if (gDebugLevel > 0) {
        printf("NOT FOUND !!!\n");
    }

    return NULL;
}

char SigprocFile::get_keyword_type(const char* keyword) {
    // Header value types: flag, integer, string, double, byte, int64, or unknown.
    // Only the types handled by SetHeaderValue can be written by that method.

    if (strcmp(keyword, "HEADER_START") == 0) {
        return 'f';
    }
    if (strcmp(keyword, "telescope_id") == 0) {
        return 'i';
    }
    if (strcmp(keyword, "machine_id") == 0) {
        return 'i';
    }
    if (strcmp(keyword, "data_type") == 0) {
        return 'i';
    }
    if (strcmp(keyword, "rawdatafile") == 0) {
        return 's';
    }
    if (strcmp(keyword, "source_name") == 0) {
        return 's';
    }
    if (strcmp(keyword, "barycentric") == 0) {
        return 'i';
    }
    if (strcmp(keyword, "pulsarcentric") == 0) {
        return 'i';
    }
    if (strcmp(keyword, "az_start") == 0) {
        return 'd';
    }
    if (strcmp(keyword, "za_start") == 0) {
        return 'd';
    }
    if (strcmp(keyword, "src_raj") == 0) {
        return 'd';
    }
    if (strcmp(keyword, "src_dej") == 0) {
        return 'd';
    }
    if (strcmp(keyword, "tstart") == 0) {
        return 'd';
    }
    if (strcmp(keyword, "tstart_utc") == 0) {
        return 's';
    }
    if (strcmp(keyword, "tsamp") == 0) {
        return 'd';
    }
    if (strcmp(keyword, "nbits") == 0) {
        return 'i';
    }
    if (strcmp(keyword, "signed") == 0) {
        return 'b';
    }
    if (strcmp(keyword, "nsamples") == 0) {
        return 'i';
    }
    if (strcmp(keyword, "nbeams") == 0) {
        return 'i';
    }
    if (strcmp(keyword, "ibeam") == 0) {
        return 'i';
    }
    if (strcmp(keyword, "fch1") == 0) {
        return 'd';
    }
    if (strcmp(keyword, "foff") == 0) {
        return 'd';
    }
    if (strcmp(keyword, "FREQUENCY_START") == 0) {
        return 'f';
    }
    if (strcmp(keyword, "fchannel") == 0) {
        return 'd';
    }
    if (strcmp(keyword, "FREQUENCY_END") == 0) {
        return 'f';
    }
    if (strcmp(keyword, "nchans") == 0) {
        return 'i';
    }
    if (strcmp(keyword, "nifs") == 0) {
        return 'i';
    }
    if (strcmp(keyword, "refdm") == 0) {
        return 'd';
    }
    if (strcmp(keyword, "period") == 0) {
        return 'd';
    }
    if (strcmp(keyword, "npuls") == 0) {
        return 'q';
    }
    if (strcmp(keyword, "nbins") == 0) {
        return 'i';
    }
    if (strcmp(keyword, "HEADER_END") == 0) {
        return 'f';
    }

    printf("ERROR : unknown type of keyword = %s\n", keyword);

    return 'u';
}

int SigprocFile::SetHeaderValue(const char* keyword, double value) {
    return SetHeaderValue(m_hdr, m_hdr_nbytes, keyword, value);
}

int SigprocFile::SetHeaderValue(const char* keyword, int value) {
    return SetHeaderValue(m_hdr, m_hdr_nbytes, keyword, value, value);
}

int SigprocFile::SetHeaderValue(char* pHeader, int header_len, const char* keyword, double value,
                                int value_int, const char* value_str, int start_index) {
    // Existing numeric values are overwritten in place. Otherwise, start_index
    // identifies where to append the length-prefixed keyword and its value.
    char* p_foff_ptr = find_string_in_header(pHeader, header_len, keyword);
    int len = strlen(keyword);
    char keyword_type = get_keyword_type(keyword);
    char szValue[16];
    memset(szValue, '\0', 16);

    if (p_foff_ptr) {
        printf("keyword %s found in header OK\n", keyword);
        char* p_value = (p_foff_ptr + len);

        double foff_value, minus_foff;
        int foff_value_int, minus_foff_int;

        switch (keyword_type) {
        case 'd':
            memcpy(szValue, p_value, sizeof(double));

            foff_value = *((double*)(szValue));
            printf("CHECK : current header value of %s  = %.8f, changing to %.8f\n", keyword,
                   foff_value, value);

            minus_foff = value;
            memcpy(p_value, &minus_foff, sizeof(double));
            break;

        case 'i':
            memcpy(szValue, p_value, sizeof(int));

            foff_value_int = *((int*)(szValue));
            printf("CHECK : current header value of %s  = %d, changing to %d\n", keyword,
                   foff_value_int, value_int);

            minus_foff_int = value_int;
            memcpy(p_value, &minus_foff_int, sizeof(int));

            break;

        default:
            printf("ERROR : unsupported type of keyword %s\n", keyword);
            break;
        }

        return 1;
    } else {
        if (start_index >= 0) {
            memcpy(pHeader + start_index, &len, sizeof(len));
            start_index += sizeof(len);

            memcpy(pHeader + start_index, keyword, len);
            start_index += len;
            int len_string = 0;

            switch (keyword_type) {
            case 'd':
                memcpy(pHeader + start_index, &value, sizeof(double));
                start_index += sizeof(double);
                break;

            case 'i':
                memcpy(pHeader + start_index, &value_int, sizeof(int));
                start_index += sizeof(int);
                break;

            case 's':
                len_string = strlen(value_str);
                memcpy(pHeader + start_index, &len_string, sizeof(int));
                start_index += sizeof(int);

                memcpy(pHeader + start_index, value_str, len_string);
                start_index += len_string;
                break;

            case 'f':
                // The keyword was already written above.
                break;

            default:
                printf("ERROR : unsupported type of keyword %s\n", keyword);
                break;
            }
        } else {
            printf("ERROR : could not find keyword %s in header\n", keyword);
        }
    }

    return start_index;
}

int SigprocFile::FillHeader(bool recalc_tstart, bool fill_radec) {
    bool b_tstart_utc = false;

    m_hdr_nbytes = MAX_HDR_SIZE;
    memset(m_hdr, '\0', m_hdr_nbytes);

    double mjd = m_tstart;
    if (recalc_tstart) {
        // The Unix epoch is MJD 40587; SIGPROC stores tstart in MJD days.
        mjd = 40587.0 + m_tstart / 86400.0;
    }

    time_t timestamp = static_cast<time_t>(m_tstart);
    struct tm utc;
    if (!gmtime_r(&timestamp, &utc)) {
        fprintf(stderr, "ERROR : cannot convert timestamp to UTC\n");
        exit(EXIT_FAILURE);
    }
    double sec = utc.tm_sec + (m_tstart - int(m_tstart));
    char szUTC[128];
    snprintf(szUTC, sizeof(szUTC), "%04d-%02d-%02dT%02d:%02d:%.3f", utc.tm_year + 1900,
             utc.tm_mon + 1, utc.tm_mday, utc.tm_hour, utc.tm_min, sec);
    printf("DEBUG : CFilFile::WriteHeader ux = %.6f -> mjd = %.8f and %s\n", m_tstart, mjd, szUTC);

    int idx = 0;
    idx = SetHeaderValue(m_hdr, m_hdr_nbytes, "HEADER_START", 0.00, 0, NULL, idx);
    idx = SetHeaderValue(m_hdr, m_hdr_nbytes, "nchans", 0.00, m_nchans, NULL, idx);
    idx = SetHeaderValue(m_hdr, m_hdr_nbytes, "nifs", 0.00, m_nifs, NULL, idx);
    idx = SetHeaderValue(m_hdr, m_hdr_nbytes, "fch1", m_fch1, 0, NULL, idx);
    idx = SetHeaderValue(m_hdr, m_hdr_nbytes, "foff", m_foff, 0, NULL, idx);
    idx = SetHeaderValue(m_hdr, m_hdr_nbytes, "tstart", mjd, 0, NULL, idx);
    if (b_tstart_utc) {
        idx = SetHeaderValue(m_hdr, m_hdr_nbytes, "tstart_utc", 0.00, 0, szUTC, idx);
    }
    idx = SetHeaderValue(m_hdr, m_hdr_nbytes, "tsamp", m_tsamp, 0, NULL, idx);
    idx = SetHeaderValue(m_hdr, m_hdr_nbytes, "nbits", 0.00, m_nbits, NULL, idx);
    idx = SetHeaderValue(m_hdr, m_hdr_nbytes, "src_raj", m_src_raj, 0, NULL, idx);
    idx = SetHeaderValue(m_hdr, m_hdr_nbytes, "src_dej", m_src_dej, 0, NULL, idx);
    idx = SetHeaderValue(m_hdr, m_hdr_nbytes, "telescope_id", 0.00, m_telescope_id, NULL, idx);
    printf("DEBUG : added src_raj, src_decj and telescope_id to the header [%.8f,%.8f,%d]\n",
           m_src_raj, m_src_dej, m_telescope_id);

    idx = SetHeaderValue(m_hdr, m_hdr_nbytes, "HEADER_END", 0.00, 0, NULL, idx);
    m_hdr_nbytes = idx;

    printf("DEBUG : set nbits := %d\n", m_nbits);

    return idx;
}

int SigprocFile::WriteData(float* buffer, int n_channels) {
    if (m_file) {
        int written = fwrite(buffer, sizeof(float), n_channels, m_file);
        if (written != n_channels) {
            printf("ERROR : could not write header written %d bytes, should be %d bytes !!!\n",
                   written, n_channels);
            fclose(m_file);
            m_file = NULL;
            m_fd = 0;
            return -n_channels;
        }

        return written * sizeof(float);
    }

    return 0;
}

bool SigprocFile::DoesFileExist(const char* fname) {
    bool bRet = false;
    if (fname && fname[0]) {
        if (access(fname, F_OK) == 0) {
            bRet = true;
        }
    }
    return bRet;
}

int64_t SigprocFile::GetFileSize(const char* filename) {
    if (!filename || !filename[0]) {
        fprintf(stderr, "ERROR : cannot determine file size: empty filename\n");
        return -1;
    }

    struct stat buf;
    if (stat(filename, &buf) != 0) {
        fprintf(stderr, "ERROR : cannot determine file size for %s: %s\n", filename,
                strerror(errno));
        return -1;
    }
    if (buf.st_size < 0) {
        fprintf(stderr, "ERROR : negative file size for %s\n", filename);
        return -1;
    }
    return static_cast<int64_t>(buf.st_size);
}

static void prepare_merge_output(SigprocFile& output, const char* filename, int channels,
                                 float foff, double*& avg_spectrum) {
    // Reuse the first input's metadata, replacing the output channel layout.
    if (!avg_spectrum) {
        avg_spectrum = new double[channels];
        printf("INFO : allocated buffer for average spectrum of %d channels\n", channels);
    }
    for (int ch = 0; ch < channels; ch++) {
        avg_spectrum[ch] = 0;
    }

    output.SetHeaderValue("nchans", channels);
    output.foff(foff);
    output.FillHeader(false, true);
    output.SetHeaderValue("nchans", channels);
    output.WriteHeader(filename, false, true);
}

static int read_merge_spectrum(SigprocFile& input, float* output, unsigned char* byte_buffer,
                               int& bytes_to_read) {
    int channels = input.nchans();
    bytes_to_read = channels * sizeof(float);
    int bytes_read = 0;
    if (input.nbits() >= 32) {
        bytes_read = input.read(output, bytes_to_read);
    } else if (input.nbits() == 8) {
        bytes_to_read = channels * sizeof(unsigned char);
        bytes_read = input.read(byte_buffer, bytes_to_read);
        // Convert only a complete input spectrum to the float output format.
        if (bytes_read == bytes_to_read) {
            for (int ch = 0; ch < channels; ch++) {
                output[ch] = byte_buffer[ch];
            }
        }
    } else {
        fprintf(stderr, "ERROR : unsupported sample size %d bits in %s\n", input.nbits(),
                input.name());
        return -1;
    }
    return bytes_read;
}

static bool finish_merge_output(SigprocFile& output, double* avg_spectrum, int channels,
                                uint64_t spectra) {
    output.Close();
    // A partial final spectrum is never included in the output or its average.
    if (spectra == 0) {
        fprintf(stderr, "ERROR : no complete spectra were merged\n");
        return false;
    }
    if (avg_spectrum) {
        for (int ch = 0; ch < channels; ch++) {
            avg_spectrum[ch] /= spectra;
        }
    }

    printf("------------------------------------------------\n");
    printf("STATISTICS :\n");
    printf("------------------------------------------------\n");
    printf("Spectra count = %" PRIu64 "\n", spectra);
    printf("------------------------------------------------\n");
    return true;
}

static int merge_oversampled_data(const std::vector<std::unique_ptr<SigprocFile>>& infiles,
                                  SigprocFile& output, double* avg_spectrum, int coarse_channels,
                                  int fine_channels, int output_channels, int skip_last,
                                  int half_overlap) {
    int untrimmed_channels = output_channels + skip_last;
    std::vector<float> out_spectrum(untrimmed_channels);
    std::vector<unsigned char> byte_buffer(fine_channels);
    std::vector<float> float_buffer(fine_channels);
    bool all_ok = true;
    uint64_t n_out_spectra = 0;
    int out_bytes_count = output_channels * sizeof(float);

    while (all_ok) {
        memset(out_spectrum.data(), 0, untrimmed_channels * sizeof(float));
        int n_read_total = 0;
        int out_channel_index = 0;

        for (int i = 0; i < coarse_channels; i++) {
            int bytes_to_read;
            int n_read = read_merge_spectrum(*infiles[i], float_buffer.data(), byte_buffer.data(),
                                             bytes_to_read);
            if (n_read < 0) {
                return -1;
            }
            if (gDebugLevel >= 2) {
                printf("DEBUG : read %d bytes -> n_read_total = %d\n", n_read, n_read_total);
            }
            if (n_read != bytes_to_read) {
                printf("END OF FILE : read %d bytes expected %d bytes -> cannot continue\n", n_read,
                       bytes_to_read);
                all_ok = false;
                break;
            }

            if (i == 0) {
                int last_ch = coarse_channels == 1 ? fine_channels : fine_channels - half_overlap;
                for (int ch = 0; ch < last_ch; ch++) {
                    out_spectrum[out_channel_index++] = float_buffer[ch];
                }
            } else {
                // Average the retained boundary with the next input's overlap,
                // then copy its selected channels into the output.
                out_spectrum[out_channel_index - 1] =
                    (out_spectrum[out_channel_index - 1] + float_buffer[half_overlap]) / 2.00;
                int last_ch = fine_channels - (half_overlap + 1);
                if (i == coarse_channels - 1) {
                    last_ch = fine_channels;
                }
                for (int ch = half_overlap; ch < last_ch; ch++) {
                    out_spectrum[out_channel_index++] = float_buffer[ch];
                }
            }
            n_read_total += n_read;
        }

        if (!all_ok) {
            printf("INFO : end of the file reached -> checking of condition skipped\n");
            printf("Written %" PRIu64 " spectra in total\n", n_out_spectra);
            break;
        }
        if (out_channel_index != untrimmed_channels) {
            printf(
                "ERROR in code : out_channel_index = %d != expected n_out_channels = %d -> exiting code\n",
                out_channel_index, untrimmed_channels);
            return -1;
        }
        if (output.WriteData(out_spectrum.data(), output_channels) != out_bytes_count) {
            fprintf(stderr, "ERROR : could not write merged spectrum\n");
            return -1;
        }

        if (avg_spectrum) {
            for (int ch = 0; ch < output_channels; ch++) {
                avg_spectrum[ch] += out_spectrum[ch];
            }
            if (gDebugLevel >= 1 && n_out_spectra == 0) {
                printf("DEBUG spectra :\n");
                for (int ch = 0; ch < output_channels; ch++) {
                    printf("\t%d %.8f\n", ch, avg_spectrum[ch]);
                }
            }
        }

        n_out_spectra++;
        if (gDebugLevel >= 1) {
            printf("DEBUG : wrote %d channels , %" PRIu64 " spectra in total\n", output_channels,
                   n_out_spectra);
        }
    }
    return finish_merge_output(output, avg_spectrum, output_channels, n_out_spectra)
               ? output_channels
               : -1;
}

int SigprocFile::MergeCoarseChannels(std::vector<std::string>& fil_file_list, const char* out_file,
                                     double*& avg_spectrum, int foff_sign) {
    int max_filfiles = fil_file_list.size();
    std::vector<std::unique_ptr<SigprocFile>> infiles;

    if (fil_file_list.size() > max_filfiles || fil_file_list.size() <= 0) {
        printf(
            "ERROR : maximum number of fil files for SigprocFile::MergeCoarseChannels exceeded (%d given , %d is limit , no files is not allowed either)\n",
            int(fil_file_list.size()), max_filfiles);
        return -1;
    }

    SigprocFile outfil_file(fil_file_list[0].c_str());
    outfil_file.nbits(32);
    outfil_file.Close();

    int n_channels_file0 = outfil_file.nchans();
    int n_out_channels = 0;
    float foff = 0.00;
    for (int i = 0; i < fil_file_list.size(); i++) {
        infiles.emplace_back(new SigprocFile(fil_file_list[i].c_str()));
        foff = (infiles[i])->foff();

        n_out_channels += (infiles[i])->nchans();
        if ((infiles[i])->nchans() != n_channels_file0) {
            printf("WARNING : fil file %s has %d (first one has %d)\n", fil_file_list[i].c_str(),
                   (infiles[i])->nchans(), n_channels_file0);
        }
        printf("DEBUG : added %d channels -> total %d channels\n", (infiles[i])->nchans(),
               n_out_channels);
    }
    printf("DEBUG : number of output channels = %d\n", n_out_channels);

    prepare_merge_output(outfil_file, out_file, n_out_channels, foff * foff_sign, avg_spectrum);
    std::vector<float> out_spectrum(n_out_channels);
    std::vector<unsigned char> char_buffer(n_out_channels);
    bool all_ok = true;
    uint64_t n_out_spectra = 0;
    int out_bytes_count = n_out_channels * sizeof(float);

    while (all_ok) {
        float* p_out_ptr = out_spectrum.data();
        memset(out_spectrum.data(), '\0', out_bytes_count);

        for (int i = 0; i < fil_file_list.size(); i++) {
            int in_channels = (infiles[i])->nchans();
            int bytes_to_read;
            int n_read =
                read_merge_spectrum(*infiles[i], p_out_ptr, char_buffer.data(), bytes_to_read);
            if (n_read < 0) {
                return -1;
            }
            if (n_read != bytes_to_read) {
                printf("END OF FILE : read %d bytes expected %d bytes -> cannot continue\n", n_read,
                       bytes_to_read);
                all_ok = false;
                break;
            }
            if (n_out_spectra <= 0) {
                printf("INFO : auto-detected sizeof sample %d bits\n", (infiles[i])->nbits());
            }
            p_out_ptr += in_channels;
        }
        if (!all_ok) {
            printf("Written %" PRIu64 " spectra in total\n", n_out_spectra);
            break;
        }
        if (outfil_file.WriteData(out_spectrum.data(), n_out_channels) != out_bytes_count) {
            fprintf(stderr, "ERROR : could not write merged spectrum\n");
            return -1;
        }
        if (avg_spectrum) {
            for (int j = 0; j < n_out_channels; j++) {
                avg_spectrum[j] += out_spectrum[j];
            }

            if (n_out_spectra == 0) {
                printf("DEBUG spectra :\n");
                for (int j = 0; j < n_out_channels; j++) {
                    printf("\t%d %.8f\n", j, avg_spectrum[j]);
                }
            }
        }
        n_out_spectra++;
    }
    return finish_merge_output(outfil_file, avg_spectrum, n_out_channels, n_out_spectra)
               ? n_out_channels
               : -1;
}

int SigprocFile::MergeOversampledCoarseChannels(std::vector<std::string>& fil_file_list,
                                                const char* out_file, double*& avg_spectrum,
                                                int foff_sign, bool bFreddaCompatibleOutput,
                                                double oversampling_factor) {
    int max_filfiles = fil_file_list.size();
    std::vector<std::unique_ptr<SigprocFile>> infiles;

    printf("INFO : SigprocFile::MergeOversampledCoarseChannels\n");

    if (!std::isfinite(oversampling_factor) || oversampling_factor <= 1.0) {
        fprintf(stderr, "ERROR : oversampling factor must be finite and greater than 1\n");
        return -1;
    }
    if (fil_file_list.size() > max_filfiles || fil_file_list.size() <= 0) {
        printf(
            "ERROR : maximum number of fil files for SigprocFile::MergeCoarseChannels exceeded (%d given , %d is limit , no files is not allowed either)\n",
            int(fil_file_list.size()), max_filfiles);
        return -1;
    }

    SigprocFile outfil_file(fil_file_list[0].c_str());
    outfil_file.nbits(32);
    outfil_file.Close();

    int n_channels_file0 = outfil_file.nchans();
    double beta = oversampling_factor;

    float foff = 0.00;
    int n_fine_ch = -1;
    for (int i = 0; i < fil_file_list.size(); i++) {
        infiles.emplace_back(new SigprocFile(fil_file_list[i].c_str()));

        foff = (infiles[i])->foff();
        n_fine_ch = (infiles[i])->nchans();

        if (n_fine_ch != n_channels_file0) {
            fprintf(stderr, "ERROR : fil file %s has %d channels (first one has %d)\n",
                    fil_file_list[i].c_str(), n_fine_ch, n_channels_file0);
            return -1;
        }
    }

    int n_coarse_ch = fil_file_list.size();
    if (n_fine_ch <= 0) {
        fprintf(stderr, "ERROR : input has no fine channels\n");
        return -1;
    }
    double nc = n_fine_ch * ((beta - 1.0) / beta);
    if (beta != default_oversampling_factor) {
        // Keep the default's historical truncation; snap other near-integer overlaps.
        double rounded_nc = std::round(nc);
        if (rounded_nc < n_fine_ch &&
            std::abs(nc - rounded_nc) <=
                4 * std::numeric_limits<double>::epsilon() * n_fine_ch) {
            nc = rounded_nc;
        }
    }
    int half_overlap = std::min(int(nc / 2), (n_fine_ch - 1) / 2);
    int n_edge_channels = (n_fine_ch - half_overlap) * 2;
    int n_centre_channels = (n_coarse_ch - 2) * (n_fine_ch - half_overlap - (half_overlap + 1));
    int n_out_channels =
        n_coarse_ch == 1 ? n_fine_ch : n_edge_channels + n_centre_channels;
    printf("DEBIG : n_fine_ch-int(nc/2) = %d , n_fine_ch-int(nc/2)-(int(nc/2)+1) = %d\n",
           (n_fine_ch - half_overlap), (n_fine_ch - half_overlap - (half_overlap + 1)));
    int skip_n_last_channels = 0;
    if (bFreddaCompatibleOutput) {
        printf("INFO : checking FREDDA requirements is enabled\n");
        std::vector<int> required_dividers;
        // Prefer larger FREDDA-compatible divisors by trimming the output tail.
        required_dividers.push_back(128);
        required_dividers.push_back(64);
        required_dividers.push_back(16);
        required_dividers.push_back(4);
        required_dividers.push_back(2);

        for (int i = 0; i < required_dividers.size(); i++) {
            int divider = required_dividers[i];
            if ((n_out_channels % divider) != 0) {
                printf(
                    "WARNING : number of output channels = %d does not divide by %d (required by FREDDA)\n",
                    n_out_channels, divider);

                int div_divider = int(n_out_channels / divider);
                int new_out_channels = div_divider * divider;

                if (new_out_channels >= divider) {
                    skip_n_last_channels = (n_out_channels - new_out_channels);
                    printf(
                        "WARNING : skipping last %d channels to make it divide by %d : %d -> %d\n",
                        skip_n_last_channels, divider, n_out_channels, new_out_channels);
                    n_out_channels = new_out_channels;
                    break;
                } else {
                    printf(
                        "WARNING : number of channels <%d (minimum dividable by %d) -> leaving unchanged at = %d\n",
                        divider, divider, n_out_channels);
                }
            }
        }
    }

    printf("INFO : number of fine channels = %d -> overlapping channels = %.2f (%.8f)\n", n_fine_ch,
           nc, nc);
    printf(
        "INFO : final number of channels in the stitched FIL file = %d (%d edge channels + %d centre channels)\n",
        n_out_channels, n_edge_channels, n_centre_channels);
    FILE* out_f = fopen("merged_fine_channels.txt", "w");
    fprintf(out_f, "%d\n", n_out_channels);
    fclose(out_f);

    prepare_merge_output(outfil_file, out_file, n_out_channels, foff * foff_sign, avg_spectrum);
    return merge_oversampled_data(infiles, outfil_file, avg_spectrum, n_coarse_ch, n_fine_ch,
                                  n_out_channels, skip_n_last_channels, half_overlap);
}

int SigprocFile::WriteHeader(const char* filename, bool bClose, bool bNewFile) {
    FILE* out_f = fopen(filename, "w");
    int ret = fwrite(m_hdr, sizeof(char), m_hdr_nbytes, out_f);

    if (bClose) {
        fclose(out_f);
    }

    if (bNewFile) {
        m_file = out_f;
        m_fd = fileno(m_file);
    }

    return ret;
}

SigprocFile::SigprocFile(int nbits, int nifs, int nchans, double fch1, double foff, double tstart,
                         double tsamp)
    : m_nbits(nbits), m_nifs(nifs), m_nchans(nchans), m_fch1(fch1), m_foff(foff), m_tstart(tstart),
      m_tsamp(tsamp), m_file(NULL), m_filename(NULL), m_hdr_nbytes(MAX_HDR_SIZE), m_src_raj(0.00),
      m_src_dej(0.00) {}

SigprocFile::SigprocFile(const char* filename, int header_size)
    : m_file(NULL), m_filename(NULL), m_samples_read(0), m_fd(0), m_nifs(0), m_nchans(0),
      m_nbits(0), m_src_raj(0.00), m_src_dej(0.00) {
    m_hdr_nbytes = MAX_HDR_SIZE;
    int64_t file_size = GetFileSize(filename);
    if (file_size < 0) {
        throw InvalidSourceFormat();
    }
    m_file_nbytes = static_cast<uint64_t>(file_size);

    m_file = fopen(filename, "r");
    m_samples_read = 0;

    if (!m_file) {
        printf("SigprocFile: could not open file: %s - %s\n", filename, strerror(errno));
        throw InvalidSourceFormat();
    }

    if (header_size < 0) {
        header_size = MAX_HDR_SIZE;
    }

    size_t size = fread(m_hdr, sizeof(char), MAX_HDR_SIZE, m_file);
    const size_t header_start_length = sizeof("HEADER_START") - 1;
    int keyword_length = 0;
    if (size >= sizeof(keyword_length)) {
        memcpy(&keyword_length, m_hdr, sizeof(keyword_length));
    }
    if (size < sizeof(keyword_length) + header_start_length ||
        keyword_length != static_cast<int>(header_start_length) ||
        memcmp(m_hdr + sizeof(keyword_length), "HEADER_START", header_start_length) != 0) {
        fprintf(stderr, "SigprocFile: File %s does not start with HEADER_START\n", filename);
        Close();
        throw InvalidSourceFormat();
    }

    // Header keywords are embedded in binary data, so a string search must
    // continue past any null bytes before HEADER_END.
    char* hdr_end = mystrnstr(m_hdr, "HEADER_END", MAX_HDR_SIZE);
    if (hdr_end == NULL) {
        printf("SigprocFile: File %s does not contain HEADER_END\n", filename);
        Close();
        throw InvalidSourceFormat();
    }
    m_filename = new char[strlen(filename) + 1];
    m_filename = strcpy(m_filename, filename);
    m_hdr_nbytes = (size_t)((hdr_end - m_hdr) + strlen("HEADER_END"));
    m_data_nbytes = m_file_nbytes - m_hdr_nbytes;
    assert(m_hdr_nbytes < MAX_HDR_SIZE);
    m_hdr[m_hdr_nbytes] = 0;
    seek_sample(0);
    printf("File %s has header %d bytes long. i.e. 0x%x\n", filename, int(m_hdr_nbytes),
           int(m_hdr_nbytes));
    m_nbits = header_int("nbits");
    m_nifs = header_int("nifs");
    m_nchans = header_int("nchans");
    m_fch1 = header_double("fch1");
    m_foff = header_double("foff");
    m_tstart = header_double("tstart");
    m_tsamp = header_double("tsamp");
    m_src_raj = header_double("src_raj");
    m_src_dej = header_double("src_dej");

    printf("DEBUG : m_tstart = %.8f (SigprocFile::SigprocFile)\n", m_tstart);

    m_fd = fileno(m_file);

#ifdef HAVE_POSIX_FADVISE
    // Hint that the input will be consumed sequentially when supported.
    if (posix_fadvise(m_fd, 0, 0, POSIX_FADV_SEQUENTIAL) != 0) {
        perror("Could not set advice\n");
        exit(EXIT_FAILURE);
    }
#endif

    // block size must be a multiple of 8 bits
    if (nbits() * nifs() * nchans() % 8 != 0) {
        printf("SigprocFile: File %s nbits=%d not supported\n", filename, nbits());
        throw InvalidSourceFormat();
    }
}

void SigprocFile::Close() {
    if (m_file) {
        fclose(m_file);
        m_file = NULL;
    }
}

SigprocFile::~SigprocFile() {
    Close();
    if (m_filename) {
        delete[] m_filename;
    }
}

const char* SigprocFile::header_find(const char* hname) const {
    char* hstart = mystrnstr(m_hdr, hname, MAX_HDR_SIZE);
    if (hstart == NULL) {
        printf("Could not find header %s in file %s\n", hname, m_filename);
        exit(EXIT_FAILURE);
    }

    // The keyword's preceding length distinguishes a full name from a substring.
    int hlen = *((int*)(hstart - sizeof(int)));
    if (hlen != strlen(hname)) {
        printf("Could not find header %s n file %s (but found a substring)\n", hname, m_filename);
        exit(EXIT_FAILURE);
    }

    char* dstart = (hstart + strlen(hname));
    return dstart;
}
double SigprocFile::header_double(const char* hname) const {

    double* vstart = (double*)header_find(hname);
    double value = *vstart;
    return value;
}

int SigprocFile::header_int(const char* hname) const {
    int* vstart = (int*)header_find(hname);
    int value = *vstart;
    return value;
}

size_t SigprocFile::seek_sample(size_t t) {
    // Skip the header and t complete time samples (all IFs and channels).
    size_t boff = t * nifs() * nchans() * nbits() / 8 + m_hdr_nbytes;
    printf("DEBUG : t = %ld , m_hdr_nbytes = %ld -> boff = %ld, file = %ld\n", t, m_hdr_nbytes,
           boff, (long int)m_file);
    if (fseek(m_file, boff, SEEK_SET) < 0) {
        printf("SigprocFile: Could not seek to offset of file %s\n. Error: %s", m_filename,
               strerror(errno));
        assert(0);
    }

    m_current_sample = t;
    return boff;
}

int SigprocFile::read(void* buffer, int size) {
    int bytes_read = fread(buffer, 1, size, m_file);
    if (ferror(m_file)) {
        fprintf(stderr, "ERROR : could not read %s: %s\n", m_filename, strerror(errno));
        return -1;
    }
    return bytes_read;
}

int SigprocFile::MergeCoarseChannels(std::vector<std::string>& fil_file_list, const char* out_file,
                                     double*& avg_spectrum, int foff_sign,
                                     uint64_t max_file_size_bytes, double max_duration_seconds) {
    int max_filfiles = fil_file_list.size();
    std::vector<std::unique_ptr<SigprocFile>> infiles;

    if (fil_file_list.size() > max_filfiles || fil_file_list.size() <= 0) {
        printf(
            "ERROR : maximum number of fil files for SigprocFile::MergeCoarseChannels exceeded (%d given , %d is limit , no files is not allowed either)\n",
            int(fil_file_list.size()), max_filfiles);
        return -1;
    }

    SigprocFile outfil_file(fil_file_list[0].c_str());
    outfil_file.nbits(32);
    outfil_file.Close();

    int n_channels_file0 = outfil_file.nchans();
    int n_out_channels = 0;
    float foff = 0.00;
    for (int i = 0; i < fil_file_list.size(); i++) {
        infiles.emplace_back(new SigprocFile(fil_file_list[i].c_str()));
        foff = (infiles[i])->foff();

        n_out_channels += (infiles[i])->nchans();
        if ((infiles[i])->nchans() != n_channels_file0) {
            printf("WARNING : fil file %s has %d (first one has %d)\n", fil_file_list[i].c_str(),
                   (infiles[i])->nchans(), n_channels_file0);
        }
        printf("DEBUG : added %d channels -> total %d channels\n", (infiles[i])->nchans(),
               n_out_channels);
    }
    printf("DEBUG : number of output channels = %d\n", n_out_channels);

    FileSplitter splitter(out_file, max_file_size_bytes, max_duration_seconds);

    if (splitter.IsSplittingEnabled()) {
        printf("INFO : File splitting enabled\n");
        if (max_file_size_bytes > 0) {
            printf("       Max file size: %" PRIu64 " bytes\n", max_file_size_bytes);
        }
        if (max_duration_seconds > 0.0) {
            printf("       Max duration: %.2f seconds\n", max_duration_seconds);
        }
    }

    prepare_merge_output(outfil_file, splitter.GetCurrentFilename().c_str(), n_out_channels,
                         foff * foff_sign, avg_spectrum);

    std::vector<float> out_spectrum(n_out_channels);
    std::vector<unsigned char> char_buffer(n_out_channels);
    bool all_ok = true;
    uint64_t n_out_spectra = 0;
    uint64_t n_out_spectra_total = 0;
    int out_bytes_count = n_out_channels * sizeof(float);
    uint64_t current_file_bytes = 0;
    double current_file_duration = 0.0;
    double tsamp = outfil_file.tsamp();
    double* null_spectrum = NULL;

    while (all_ok) {
        float* p_out_ptr = out_spectrum.data();
        memset(out_spectrum.data(), '\0', out_bytes_count);

        for (int i = 0; i < fil_file_list.size(); i++) {
            int in_channels = (infiles[i])->nchans();
            int bytes_to_read;
            int n_read =
                read_merge_spectrum(*infiles[i], p_out_ptr, char_buffer.data(), bytes_to_read);
            if (n_read < 0) {
                return -1;
            }
            if (n_read != bytes_to_read) {
                printf("END OF FILE : read %d bytes expected %d bytes -> cannot continue\n", n_read,
                       bytes_to_read);
                all_ok = false;
                break;
            }
            if (n_out_spectra <= 0) {
                printf("INFO : auto-detected sizeof sample %d bits\n", (infiles[i])->nbits());
            }
            p_out_ptr += in_channels;
        }
        if (!all_ok) {
            printf("Written %" PRIu64 " spectra in total\n", n_out_spectra_total);
            break;
        }

        if (splitter.IsSplittingEnabled() && n_out_spectra > 0) {
            uint64_t projected_bytes = current_file_bytes + out_bytes_count;
            double projected_duration = current_file_duration + tsamp;

            if (splitter.NeedsSplit(projected_bytes, projected_duration)) {
                printf("INFO : File size/duration limit reached, rotating to next file\n");
                printf("       Current file bytes: %" PRIu64 ", duration: %.2f seconds, spectra: %" PRIu64 "\n",
                       current_file_bytes, current_file_duration, n_out_spectra);

                finish_merge_output(outfil_file, null_spectrum, n_out_channels, n_out_spectra);

                splitter.AdvanceToNextFile();
                printf("INFO : Creating new output file: %s\n", splitter.GetCurrentFilename().c_str());

                prepare_merge_output(outfil_file, splitter.GetCurrentFilename().c_str(), n_out_channels,
                                     foff * foff_sign, null_spectrum);

                current_file_bytes = 0;
                current_file_duration = 0.0;
                n_out_spectra = 0;
            }
        }

        if (outfil_file.WriteData(out_spectrum.data(), n_out_channels) != out_bytes_count) {
            fprintf(stderr, "ERROR : could not write merged spectrum\n");
            return -1;
        }

        current_file_bytes += out_bytes_count;
        current_file_duration += tsamp;

        if (avg_spectrum) {
            for (int j = 0; j < n_out_channels; j++) {
                avg_spectrum[j] += out_spectrum[j];
            }

            if (n_out_spectra_total == 0) {
                printf("DEBUG spectra :\n");
                for (int j = 0; j < n_out_channels; j++) {
                    printf("\t%d %.8f\n", j, avg_spectrum[j]);
                }
            }
        }
        n_out_spectra++;
        n_out_spectra_total++;
    }

    // Always call finish_merge_output to handle error reporting for zero spectra
    if (!finish_merge_output(outfil_file, null_spectrum, n_out_channels, n_out_spectra)) {
        return -1;
    }

    if (avg_spectrum && n_out_spectra_total > 0) {
        for (int ch = 0; ch < n_out_channels; ch++) {
            avg_spectrum[ch] /= n_out_spectra_total;
        }
    }

    std::vector<std::string> all_files = splitter.GetCreatedFiles();
    if (all_files.size() > 1) {
        printf("INFO : Merge complete with %zu output files\n", all_files.size());
        for (size_t i = 0; i < all_files.size(); i++) {
            printf("       [%zu] %s\n", i + 1, all_files[i].c_str());
        }
    }

    return n_out_spectra_total > 0 ? n_out_channels : -1;
}

int SigprocFile::MergeOversampledCoarseChannels(std::vector<std::string>& fil_file_list,
                                                const char* out_file, double*& avg_spectrum,
                                                int foff_sign, bool bFreddaCompatibleOutput,
                                                double oversampling_factor,
                                                uint64_t max_file_size_bytes,
                                                double max_duration_seconds) {
    int max_filfiles = fil_file_list.size();
    std::vector<std::unique_ptr<SigprocFile>> infiles;

    printf("INFO : SigprocFile::MergeOversampledCoarseChannels (with file splitting)\n");

    if (!std::isfinite(oversampling_factor) || oversampling_factor <= 1.0) {
        fprintf(stderr, "ERROR : oversampling factor must be finite and greater than 1\n");
        return -1;
    }
    if (fil_file_list.size() > max_filfiles || fil_file_list.size() <= 0) {
        printf(
            "ERROR : maximum number of fil files for SigprocFile::MergeCoarseChannels exceeded (%d given , %d is limit , no files is not allowed either)\n",
            int(fil_file_list.size()), max_filfiles);
        return -1;
    }

    SigprocFile outfil_file(fil_file_list[0].c_str());
    outfil_file.nbits(32);
    outfil_file.Close();

    int n_channels_file0 = outfil_file.nchans();
    double beta = oversampling_factor;

    float foff = 0.00;
    int n_fine_ch = -1;
    for (int i = 0; i < fil_file_list.size(); i++) {
        infiles.emplace_back(new SigprocFile(fil_file_list[i].c_str()));

        foff = (infiles[i])->foff();
        n_fine_ch = (infiles[i])->nchans();

        if (n_fine_ch != n_channels_file0) {
            fprintf(stderr, "ERROR : fil file %s has %d channels (first one has %d)\n",
                    fil_file_list[i].c_str(), n_fine_ch, n_channels_file0);
            return -1;
        }
    }

    int n_coarse_ch = fil_file_list.size();
    if (n_fine_ch <= 0) {
        fprintf(stderr, "ERROR : input has no fine channels\n");
        return -1;
    }
    double nc = n_fine_ch * ((beta - 1.0) / beta);
    if (beta != default_oversampling_factor) {
        double rounded_nc = std::round(nc);
        if (rounded_nc < n_fine_ch &&
            std::abs(nc - rounded_nc) <=
                4 * std::numeric_limits<double>::epsilon() * n_fine_ch) {
            nc = rounded_nc;
        }
    }
    int half_overlap = std::min(int(nc / 2), (n_fine_ch - 1) / 2);
    int n_edge_channels = (n_fine_ch - half_overlap) * 2;
    int n_centre_channels = (n_coarse_ch - 2) * (n_fine_ch - half_overlap - (half_overlap + 1));
    int n_out_channels =
        n_coarse_ch == 1 ? n_fine_ch : n_edge_channels + n_centre_channels;
    printf("DEBIG : n_fine_ch-int(nc/2) = %d , n_fine_ch-int(nc/2)-(int(nc/2)+1) = %d\n",
           (n_fine_ch - half_overlap), (n_fine_ch - half_overlap - (half_overlap + 1)));
    int skip_n_last_channels = 0;
    if (bFreddaCompatibleOutput) {
        printf("INFO : checking FREDDA requirements is enabled\n");
        std::vector<int> required_dividers;
        required_dividers.push_back(128);
        required_dividers.push_back(64);
        required_dividers.push_back(16);
        required_dividers.push_back(4);
        required_dividers.push_back(2);

        for (int i = 0; i < required_dividers.size(); i++) {
            int divider = required_dividers[i];
            if ((n_out_channels % divider) != 0) {
                printf(
                    "WARNING : number of output channels = %d does not divide by %d (required by FREDDA)\n",
                    n_out_channels, divider);

                int div_divider = int(n_out_channels / divider);
                int new_out_channels = div_divider * divider;

                if (new_out_channels >= divider) {
                    skip_n_last_channels = (n_out_channels - new_out_channels);
                    printf(
                        "WARNING : skipping last %d channels to make it divide by %d : %d -> %d\n",
                        skip_n_last_channels, divider, n_out_channels, new_out_channels);
                    n_out_channels = new_out_channels;
                    break;
                } else {
                    printf(
                        "WARNING : number of channels <%d (minimum dividable by %d) -> leaving unchanged at = %d\n",
                        divider, divider, n_out_channels);
                }
            }
        }
    }

    printf("INFO : number of fine channels = %d -> overlapping channels = %.2f (%.8f)\n", n_fine_ch,
           nc, nc);
    printf(
        "INFO : final number of channels in the stitched FIL file = %d (%d edge channels + %d centre channels)\n",
        n_out_channels, n_edge_channels, n_centre_channels);
    FILE* out_f = fopen("merged_fine_channels.txt", "w");
    fprintf(out_f, "%d\n", n_out_channels);
    fclose(out_f);

    FileSplitter splitter(out_file, max_file_size_bytes, max_duration_seconds);

    if (splitter.IsSplittingEnabled()) {
        printf("INFO : File splitting enabled for oversampled merge\n");
        if (max_file_size_bytes > 0) {
            printf("       Max file size: %" PRIu64 " bytes\n", max_file_size_bytes);
        }
        if (max_duration_seconds > 0.0) {
            printf("       Max duration: %.2f seconds\n", max_duration_seconds);
        }
    }

    prepare_merge_output(outfil_file, splitter.GetCurrentFilename().c_str(), n_out_channels,
                         foff * foff_sign, avg_spectrum);

    int untrimmed_channels = n_out_channels + skip_n_last_channels;
    std::vector<float> out_spectrum(untrimmed_channels);
    std::vector<unsigned char> byte_buffer(n_fine_ch);
    std::vector<float> float_buffer(n_fine_ch);
    bool all_ok = true;
    uint64_t n_out_spectra = 0;
    uint64_t n_out_spectra_total = 0;
    int out_bytes_count = n_out_channels * sizeof(float);
    uint64_t current_file_bytes = 0;
    double current_file_duration = 0.0;
    double tsamp = outfil_file.tsamp();
    double* null_spectrum = NULL;

    while (all_ok) {
        memset(out_spectrum.data(), 0, untrimmed_channels * sizeof(float));
        int n_read_total = 0;
        int out_channel_index = 0;

        for (int i = 0; i < n_coarse_ch; i++) {
            int bytes_to_read;
            int n_read = read_merge_spectrum(*infiles[i], float_buffer.data(), byte_buffer.data(),
                                             bytes_to_read);
            if (n_read < 0) {
                return -1;
            }
            if (gDebugLevel >= 2) {
                printf("DEBUG : read %d bytes -> n_read_total = %d\n", n_read, n_read_total);
            }
            if (n_read != bytes_to_read) {
                printf("END OF FILE : read %d bytes expected %d bytes -> cannot continue\n", n_read,
                       bytes_to_read);
                all_ok = false;
                break;
            }

            if (i == 0) {
                int last_ch = n_coarse_ch == 1 ? n_fine_ch : n_fine_ch - half_overlap;
                for (int ch = 0; ch < last_ch; ch++) {
                    out_spectrum[out_channel_index++] = float_buffer[ch];
                }
            } else {
                out_spectrum[out_channel_index - 1] =
                    (out_spectrum[out_channel_index - 1] + float_buffer[half_overlap]) / 2.00;
                int last_ch = n_fine_ch - (half_overlap + 1);
                if (i == n_coarse_ch - 1) {
                    last_ch = n_fine_ch;
                }
                for (int ch = half_overlap; ch < last_ch; ch++) {
                    out_spectrum[out_channel_index++] = float_buffer[ch];
                }
            }
            n_read_total += n_read;
        }

        if (!all_ok) {
            printf("INFO : end of the file reached -> checking of condition skipped\n");
            printf("Written %" PRIu64 " spectra in total\n", n_out_spectra_total);
            break;
        }
        if (out_channel_index != untrimmed_channels) {
            printf(
                "ERROR in code : out_channel_index = %d != expected n_out_channels = %d -> exiting code\n",
                out_channel_index, untrimmed_channels);
            return -1;
        }
        if (outfil_file.WriteData(out_spectrum.data(), n_out_channels) != out_bytes_count) {
            fprintf(stderr, "ERROR : could not write merged spectrum\n");
            return -1;
        }

        if (avg_spectrum) {
            for (int ch = 0; ch < n_out_channels; ch++) {
                avg_spectrum[ch] += out_spectrum[ch];
            }
            if (gDebugLevel >= 1 && n_out_spectra_total == 0) {
                printf("DEBUG spectra :\n");
                for (int ch = 0; ch < n_out_channels; ch++) {
                    printf("\t%d %.8f\n", ch, avg_spectrum[ch]);
                }
            }
        }

        n_out_spectra++;
        n_out_spectra_total++;
        current_file_bytes += out_bytes_count;
        current_file_duration += tsamp;

        if (gDebugLevel >= 1) {
            printf("DEBUG : wrote %d channels , %" PRIu64 " spectra in total\n", n_out_channels,
                   n_out_spectra_total);
        }

        // Check if we need to split
        if (splitter.NeedsSplit(current_file_bytes, current_file_duration)) {
            printf("INFO : Splitting file (bytes: %" PRIu64 ", duration: %.2fs)\n",
                   current_file_bytes, current_file_duration);
            finish_merge_output(outfil_file, null_spectrum, n_out_channels, n_out_spectra);
            splitter.AdvanceToNextFile();
            prepare_merge_output(outfil_file, splitter.GetCurrentFilename().c_str(), n_out_channels,
                                 foff * foff_sign, null_spectrum);
            current_file_bytes = 0;
            current_file_duration = 0.0;
            n_out_spectra = 0;
        }
    }

    // Always call finish_merge_output to handle error reporting for zero spectra
    if (!finish_merge_output(outfil_file, null_spectrum, n_out_channels, n_out_spectra)) {
        return -1;
    }

    if (avg_spectrum && n_out_spectra_total > 0) {
        for (int ch = 0; ch < n_out_channels; ch++) {
            avg_spectrum[ch] /= n_out_spectra_total;
        }
    }

    std::vector<std::string> all_files = splitter.GetCreatedFiles();
    if (all_files.size() > 1) {
        printf("INFO : Merge complete with %zu output files\n", all_files.size());
        for (size_t i = 0; i < all_files.size(); i++) {
            printf("       [%zu] %s\n", i + 1, all_files[i].c_str());
        }
    }

    return n_out_spectra_total > 0 ? n_out_channels : -1;
}

size_t SigprocFile::read_samples(void** output) {
    // Usually we use the buffer in the FileSet, so we'll just ignore this for now.
    assert(0);
    return 0;
}
