/**
 * Aften: A/52 audio encoder
 * Copyright (c) 2006  Justin Ruggles <justinruggles@bellsouth.net>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file wav.c
 * WAV decoder
 */

#include "common.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "wav.h"

#define RIFF_ID     0x46464952
#define WAVE_ID     0x45564157
#define FMT__ID     0x20746D66
#define DATA_ID     0x61746164

/**
 * Reads a 4-byte little-endian word from the input stream
 */
static inline uint32_t
read4le(FILE *fp)
{
    uint32_t x;
    fread(&x, 4, 1, fp);
    if(feof(fp)) return 0;
    return le2me_32(x);
}

/**
 * Reads a 2-byte little-endian word from the input stream
 */
static uint16_t
read2le(FILE *fp)
{
    uint16_t x;
    fread(&x, 2, 1, fp);
    if(feof(fp)) return 0;
    return le2me_16(x);
}

/**
 * Seeks to byte offset within file.
 * Limits the seek position or offset to signed 32-bit.
 * It also does slower forward seeking for streaming input.
 */
static inline int
aft_seek_set(WavFile *wf, uint64_t dest)
{
    int slow_seek = !(wf->seekable);

    if(wf->seekable) {
        if(dest <= INT32_MAX) {
            // destination is within first 2GB
            if(fseek(wf->fp, (long)dest, SEEK_SET)) return -1;
        } else {
            int64_t offset = (int64_t)dest - (int64_t)wf->filepos;
            if(offset >= INT32_MIN && offset <= INT32_MAX) {
                // offset is within +/- 2GB of file start
                if(fseek(wf->fp, (long)offset, SEEK_CUR)) return -1;
            } else {
                // absolute offset is more than 2GB
                if(offset < 0) {
                    fprintf(stderr, "error: backward seeking is limited to 2GB\n");
                    return -1;
                } else {
                    fprintf(stderr, "warning: forward seeking more than 2GB will be slow.\n");
                }
                slow_seek = 1;
            }
            
        }
    }
    if(slow_seek) {
        // do forward-only seek by reading data to temp buffer
        uint64_t offset;
        uint8_t buf[1024];
        int c=0;
        if(dest < wf->filepos) return -1;
        offset = dest - wf->filepos;
        while(offset > 1024) {
            fread(buf, 1, 1024, wf->fp);
            offset -= 1024;
        }
        while(offset-- > 0 && c != EOF) {
            c = fgetc(wf->fp);
        }
    }
    wf->filepos = dest;
    return 0;
}

/**
 * Initializes WavFile structure using the given input file pointer.
 * Examines the wave header to get audio information and has the file
 * pointer aligned at start of data when it exits.
 * Returns non-zero value if an error occurs.
 */
int
wavfile_init(WavFile *wf, FILE *fp)
{
    int id, found_fmt, found_data;
    uint32_t chunksize;

    if(wf == NULL || fp == NULL) {
        fprintf(stderr, "null input to wav reader\n");
        return -1;
    }

    memset(wf, 0, sizeof(WavFile));
    wf->fp = fp;

    // attempt to get file size
    wf->file_size = 0;
#ifdef _WIN32
    // in Windows, don't try to detect seeking support for stdin
    if(fp != stdin) {
        wf->seekable = !fseek(fp, 0, SEEK_END);
    }
#else
    wf->seekable = !fseek(fp, 0, SEEK_END);
#endif
    if(wf->seekable) {
        // TODO: portable 64-bit ftell
        long fs = ftell(fp);
        // ftell should return an error if value cannot fit in return type
        if(fs < 0) {
            fprintf(stderr, "Warning, unsupported file size.\n");
            wf->file_size = 0;
        } else {
            wf->file_size = (uint64_t)fs;
        }
        fseek(fp, 0, SEEK_SET);
    }
    wf->filepos = 0;

    // read RIFF id. ignore size.
    id = read4le(fp);
    wf->filepos += 4;
    if(id != RIFF_ID) {
        fprintf(stderr, "invalid RIFF id in wav header\n");
        return -1;
    }
    read4le(fp);
    wf->filepos += 4;

    // read WAVE id. ignore size.
    id = read4le(fp);
    wf->filepos += 4;
    if(id != WAVE_ID) {
        fprintf(stderr, "invalid WAVE id in wav header\n");
        return -1;
    }

    // read all header chunks. skip unknown chunks.
    found_data = found_fmt = 0;
    while(!found_data) {
        id = read4le(fp);
        wf->filepos += 4;
        chunksize = read4le(fp);
        wf->filepos += 4;
        if(id == 0 || chunksize == 0) {
            fprintf(stderr, "invalid or empty chunk in wav header\n");
            return -1;
        }
        switch(id) {
            case FMT__ID:
                if(chunksize < 16) {
                    fprintf(stderr, "invalid fmt chunk in wav header\n");
                    return -1;
                }
                wf->format = read2le(fp);
                wf->filepos += 2;
                wf->channels = read2le(fp);
                wf->filepos += 2;
                if(wf->channels == 0) {
                    fprintf(stderr, "invalid number of channels in wav header\n");
                    return -1;
                }
                wf->sample_rate = read4le(fp);
                wf->filepos += 4;
                if(wf->sample_rate == 0) {
                    fprintf(stderr, "invalid sample rate in wav header\n");
                    return -1;
                }
                read4le(fp);
                wf->filepos += 4;
                wf->block_align = read2le(fp);
                wf->filepos += 2;
                wf->bit_width = read2le(fp);
                wf->filepos += 2;
                if(wf->bit_width == 0) {
                    fprintf(stderr, "invalid sample bit width in wav header\n");
                    return -1;
                }
                chunksize -= 16;

                // WAVE_FORMAT_EXTENSIBLE data
                wf->ch_mask = 0;
                if(wf->format == WAVE_FORMAT_EXTENSIBLE && chunksize >= 10) {
                    read4le(fp);    // skip CbSize and ValidBitsPerSample
                    wf->filepos += 4;
                    wf->ch_mask = read4le(fp);
                    wf->filepos += 4;
                    wf->format = read2le(fp);
                    wf->filepos += 2;
                    chunksize -= 10;
                }

                if(wf->format == WAVE_FORMAT_PCM || wf->format == WAVE_FORMAT_IEEEFLOAT) {
                    // override block alignment in header for uncompressed pcm
                    wf->block_align = MAX(1, ((wf->bit_width + 7) >> 3) * wf->channels);
                }
                wf->bytes_per_sec = wf->sample_rate * wf->block_align;

                // make up channel mask if not using WAVE_FORMAT_EXTENSIBLE
                // or if ch_mask is set to zero (unspecified configuration)
                // TODO: select default configurations for >6 channels
                if(wf->ch_mask == 0) {
                    switch(wf->channels) {
                        case 1: wf->ch_mask = 0x04;  break;
                        case 2: wf->ch_mask = 0x03;  break;
                        case 3: wf->ch_mask = 0x07;  break;
                        case 4: wf->ch_mask = 0x107; break;
                        case 5: wf->ch_mask = 0x37;  break;
                        case 6: wf->ch_mask = 0x3F;  break;
                    }
                }

                // skip any leftover bytes in fmt chunk
                if(aft_seek_set(wf, wf->filepos + chunksize)) {
                    fprintf(stderr, "error seeking in wav file\n");
                    return -1;
                }
                found_fmt = 1;
                break;
            case DATA_ID:
                if(!found_fmt) return -1;
                wf->data_size = chunksize;
                wf->data_start = wf->filepos;
                if(wf->seekable && wf->file_size > 0) {
                    // limit data size to end-of-file
                    wf->data_size = MIN(wf->data_size, wf->file_size - wf->data_start);
                }
                wf->samples = (wf->data_size / wf->block_align);
                found_data = 1;
                break;
            default:
                // skip unknown chunk
                if(aft_seek_set(wf, wf->filepos + chunksize)) {
                    fprintf(stderr, "error seeking in wav file\n");
                    return -1;
                }
        }
    }

    // set audio data format based on bit depth and twocc format code
    wf->source_format = WAV_SAMPLE_FMT_UNKNOWN;
    wf->read_format = wf->source_format;
    if(wf->format == WAVE_FORMAT_PCM || wf->format == WAVE_FORMAT_IEEEFLOAT) {
        switch(wf->bit_width) {
            case 8:  wf->source_format = WAV_SAMPLE_FMT_U8;   break;
            case 16:  wf->source_format = WAV_SAMPLE_FMT_S16; break;
            case 20:  wf->source_format = WAV_SAMPLE_FMT_S20; break;
            case 24:  wf->source_format = WAV_SAMPLE_FMT_S24; break;
            case 32:
                if(wf->format == WAVE_FORMAT_IEEEFLOAT) {
                    wf->source_format = WAV_SAMPLE_FMT_FLT;
                } else if(wf->format == WAVE_FORMAT_PCM) {
                    wf->source_format = WAV_SAMPLE_FMT_S32;
                }
                break;
            case 64:
                if(wf->format == WAVE_FORMAT_IEEEFLOAT) {
                    wf->source_format = WAV_SAMPLE_FMT_DBL;
                }
                break;
        }
    }

    return 0;
}

static void
fmt_convert_to_u8(uint8_t *dest, void *src_v, int n, enum WavSampleFormat fmt)
{
    int i, v;

    if(fmt == WAV_SAMPLE_FMT_U8) {
        memcpy(dest, src_v, n);
    } else if(fmt == WAV_SAMPLE_FMT_S16) {
        int16_t *src = src_v;
        for(i=0; i<n; i++) {
            dest[i] = (src[i] >> 8) + 128;
        }
    } else if(fmt == WAV_SAMPLE_FMT_S20) {
        int32_t *src = src_v;
        for(i=0; i<n; i++) {
            dest[i] = (src[i] >> 12) + 128;
        }
    } else if(fmt == WAV_SAMPLE_FMT_S24) {
        int32_t *src = src_v;
        for(i=0; i<n; i++) {
            dest[i] = (src[i] >> 16) + 128;
        }
    } else if(fmt == WAV_SAMPLE_FMT_S32) {
        int32_t *src = src_v;
        for(i=0; i<n; i++) {
            dest[i] = (src[i] >> 24) + 128;
        }
    } else if(fmt == WAV_SAMPLE_FMT_FLT) {
        float *src = src_v;
        for(i=0; i<n; i++) {
            v = (int)CLIP(((src[i] * 128) + 128), 0, 255);
            dest[i] = v;
        }
    } else if(fmt == WAV_SAMPLE_FMT_DBL) {
        double *src = src_v;
        for(i=0; i<n; i++) {
            v = (int)CLIP(((src[i] * 128) + 128), 0, 255);
            dest[i] = v;
        }
    }
}

static void
fmt_convert_to_s16(int16_t *dest, void *src_v, int n, enum WavSampleFormat fmt)
{
    int i, v;

    if(fmt == WAV_SAMPLE_FMT_U8) {
        uint8_t *src = src_v;
        for(i=0; i<n; i++) {
            dest[i] = (src[i] - 128) << 8;
        }
    } else if(fmt == WAV_SAMPLE_FMT_S16) {
        memcpy(dest, src_v, n * sizeof(int16_t));
    } else if(fmt == WAV_SAMPLE_FMT_S20) {
        int32_t *src = src_v;
        for(i=0; i<n; i++) {
            dest[i] = (src[i] >> 4);
        }
    } else if(fmt == WAV_SAMPLE_FMT_S24) {
        int32_t *src = src_v;
        for(i=0; i<n; i++) {
            dest[i] = (src[i] >> 8);
        }
    } else if(fmt == WAV_SAMPLE_FMT_S32) {
        int32_t *src = src_v;
        for(i=0; i<n; i++) {
            dest[i] = (src[i] >> 16);
        }
    } else if(fmt == WAV_SAMPLE_FMT_FLT) {
        float *src = src_v;
        for(i=0; i<n; i++) {
            v = (int)CLIP((src[i] * 32768), -32768, 32767);
            dest[i] = v;
        }
    } else if(fmt == WAV_SAMPLE_FMT_DBL) {
        double *src = src_v;
        for(i=0; i<n; i++) {
            v = (int)CLIP((src[i] * 32768), -32768, 32767);
            dest[i] = v;
        }
    }
}

static void
fmt_convert_to_s20(int32_t *dest, void *src_v, int n, enum WavSampleFormat fmt)
{
    int i, v;

    if(fmt == WAV_SAMPLE_FMT_U8) {
        uint8_t *src = src_v;
        for(i=0; i<n; i++) {
            dest[i] = (src[i] - 128) << 12;
        }
    } else if(fmt == WAV_SAMPLE_FMT_S16) {
        int16_t *src = src_v;
        for(i=0; i<n; i++) {
            dest[i] = (src[i] << 4);
        }
    } else if(fmt == WAV_SAMPLE_FMT_S20) {
        memcpy(dest, src_v, n * sizeof(int32_t));
    } else if(fmt == WAV_SAMPLE_FMT_S24) {
        int32_t *src = src_v;
        for(i=0; i<n; i++) {
            dest[i] = (src[i] >> 4);
        }
    } else if(fmt == WAV_SAMPLE_FMT_S32) {
        int32_t *src = src_v;
        for(i=0; i<n; i++) {
            dest[i] = (src[i] >> 12);
        }
    } else if(fmt == WAV_SAMPLE_FMT_FLT) {
        float *src = src_v;
        for(i=0; i<n; i++) {
            v = (int)CLIP((src[i] * 524288), -524288, 524287);
            dest[i] = v;
        }
    } else if(fmt == WAV_SAMPLE_FMT_DBL) {
        double *src = src_v;
        for(i=0; i<n; i++) {
            v = (int)CLIP((src[i] * 524288), -524288, 524287);
            dest[i] = v;
        }
    }
}

static void
fmt_convert_to_s24(int32_t *dest, void *src_v, int n, enum WavSampleFormat fmt)
{
    int i, v;

    if(fmt == WAV_SAMPLE_FMT_U8) {
        uint8_t *src = src_v;
        for(i=0; i<n; i++) {
            dest[i] = (src[i] - 128) << 16;
        }
    } else if(fmt == WAV_SAMPLE_FMT_S16) {
        int16_t *src = src_v;
        for(i=0; i<n; i++) {
            dest[i] = (src[i] << 8);
        }
    } else if(fmt == WAV_SAMPLE_FMT_S20) {
        int32_t *src = src_v;
        for(i=0; i<n; i++) {
            dest[i] = (src[i] << 4);
        }
    } else if(fmt == WAV_SAMPLE_FMT_S24) {
        memcpy(dest, src_v, n * sizeof(int32_t));
    } else if(fmt == WAV_SAMPLE_FMT_S32) {
        int32_t *src = src_v;
        for(i=0; i<n; i++) {
            dest[i] = (src[i] >> 8);
        }
    } else if(fmt == WAV_SAMPLE_FMT_FLT) {
        float *src = src_v;
        for(i=0; i<n; i++) {
            v = (int)CLIP((src[i] * 8388608), -8388608, 8388607);
            dest[i] = v;
        }
    } else if(fmt == WAV_SAMPLE_FMT_DBL) {
        double *src = src_v;
        for(i=0; i<n; i++) {
            v = (int)CLIP((src[i] * 8388608), -8388608, 8388607);
            dest[i] = v;
        }
    }
}

static void
fmt_convert_to_s32(int32_t *dest, void *src_v, int n, enum WavSampleFormat fmt)
{
    int i, v;

    if(fmt == WAV_SAMPLE_FMT_U8) {
        uint8_t *src = src_v;
        for(i=0; i<n; i++) {
            dest[i] = (src[i] - 128) << 24;
        }
    } else if(fmt == WAV_SAMPLE_FMT_S16) {
        int16_t *src = src_v;
        for(i=0; i<n; i++) {
            dest[i] = (src[i] << 16);
        }
    } else if(fmt == WAV_SAMPLE_FMT_S20) {
        int32_t *src = src_v;
        for(i=0; i<n; i++) {
            dest[i] = (src[i] << 12);
        }
    } else if(fmt == WAV_SAMPLE_FMT_S24) {
        int32_t *src = src_v;
        for(i=0; i<n; i++) {
            dest[i] = (src[i] << 8);
        }
    } else if(fmt == WAV_SAMPLE_FMT_S32) {
        memcpy(dest, src_v, n * sizeof(int32_t));
    } else if(fmt == WAV_SAMPLE_FMT_FLT) {
        float *src = src_v;
        for(i=0; i<n; i++) {
            v = (int)(src[i] * 2147483648LL);
            dest[i] = v;
        }
    } else if(fmt == WAV_SAMPLE_FMT_DBL) {
        double *src = src_v;
        for(i=0; i<n; i++) {
            v = (int)(src[i] * 2147483648LL);
            dest[i] = v;
        }
    }
}

static void
fmt_convert_to_float(float *dest, void *src_v, int n, enum WavSampleFormat fmt)
{
    int i;

    if(fmt == WAV_SAMPLE_FMT_U8) {
        uint8_t *src = src_v;
        for(i=0; i<n; i++) {
            dest[i] = (src[i] - FCONST(128.0)) / FCONST(128.0);
        }
    } else if(fmt == WAV_SAMPLE_FMT_S16) {
        int16_t *src = src_v;
        for(i=0; i<n; i++) {
            dest[i] = src[i] / FCONST(32768.0);
        }
    } else if(fmt == WAV_SAMPLE_FMT_S20) {
        int32_t *src = src_v;
        for(i=0; i<n; i++) {
            dest[i] = src[i] / FCONST(524288.0);
        }
    } else if(fmt == WAV_SAMPLE_FMT_S24) {
        int32_t *src = src_v;
        for(i=0; i<n; i++) {
            dest[i] = src[i] / FCONST(8388608.0);
        }
    } else if(fmt == WAV_SAMPLE_FMT_S32) {
        int32_t *src = src_v;
        for(i=0; i<n; i++) {
            dest[i] = src[i] / FCONST(2147483648.0);
        }
    } else if(fmt == WAV_SAMPLE_FMT_FLT) {
        memcpy(dest, src_v, n * sizeof(float));
    } else if(fmt == WAV_SAMPLE_FMT_DBL) {
        double *src = src_v;
        for(i=0; i<n; i++) {
            dest[i] = (float)src[i];
        }
    }
}

static void
fmt_convert_to_double(double *dest, void *src_v, int n, enum WavSampleFormat fmt)
{
    int i;

    if(fmt == WAV_SAMPLE_FMT_U8) {
        uint8_t *src = src_v;
        for(i=0; i<n; i++) {
            dest[i] = (src[i] - 128.0) / 128.0;
        }
    } else if(fmt == WAV_SAMPLE_FMT_S16) {
        int16_t *src = src_v;
        for(i=0; i<n; i++) {
            dest[i] = src[i] / 32768.0;
        }
    } else if(fmt == WAV_SAMPLE_FMT_S20) {
        int32_t *src = src_v;
        for(i=0; i<n; i++) {
            dest[i] = src[i] / 524288.0;
        }
    } else if(fmt == WAV_SAMPLE_FMT_S24) {
        int32_t *src = src_v;
        for(i=0; i<n; i++) {
            dest[i] = src[i] / 8388608.0;
        }
    } else if(fmt == WAV_SAMPLE_FMT_S32) {
        int32_t *src = src_v;
        for(i=0; i<n; i++) {
            dest[i] = src[i] / 2147483648.0;
        }
    } else if(fmt == WAV_SAMPLE_FMT_FLT) {
        float *src = src_v;
        for(i=0; i<n; i++) {
            dest[i] = src[i];
        }
    } else if(fmt == WAV_SAMPLE_FMT_DBL) {
        memcpy(dest, src_v, n * sizeof(double));
    }
}

static void
fmt_convert(enum WavSampleFormat dest_fmt, void *dest,
            enum WavSampleFormat src_fmt, void *src, int n)
{
    switch(dest_fmt) {
        case WAV_SAMPLE_FMT_U8:
            fmt_convert_to_u8(dest, src, n, src_fmt);
            break;
        case WAV_SAMPLE_FMT_S16:
            fmt_convert_to_s16(dest, src, n, src_fmt);
            break;
        case WAV_SAMPLE_FMT_S20:
            fmt_convert_to_s20(dest, src, n, src_fmt);
            break;
        case WAV_SAMPLE_FMT_S24:
            fmt_convert_to_s24(dest, src, n, src_fmt);
            break;
        case WAV_SAMPLE_FMT_S32:
            fmt_convert_to_s32(dest, src, n, src_fmt);
            break;
        case WAV_SAMPLE_FMT_FLT:
            fmt_convert_to_float(dest, src, n, src_fmt);
            break;
        case WAV_SAMPLE_FMT_DBL:
            fmt_convert_to_double(dest, src, n, src_fmt);
            break;
        default:
            break;
    }
}

/**
 * Reads audio samples to the output buffer.
 * Output is channel-interleaved, native byte order.
 * Only up to WAVE_MAX_READ samples can be read in one call.
 * The output sample format depends on the value of wf->read_format.
 * Returns number of samples read or -1 on error.
 */
int
wavfile_read_samples(WavFile *wf, void *output, int num_samples)
{
    int nr, i, j, bps, nsmp, v, br;
    uint32_t bytes_needed;
    uint8_t *buffer;

    // check input and limit number of samples
    if(wf == NULL || wf->fp == NULL || output == NULL) {
        fprintf(stderr, "null input to wavfile_read_samples\n");
        return -1;
    }
    if(wf->block_align <= 0) {
        fprintf(stderr, "invalid block_align\n");
        return -1;
    }
    num_samples = MIN(num_samples, WAV_MAX_READ);

    // calculate number of bytes to read, being careful not to read past
    // the end of the data chunk
    bytes_needed = wf->block_align * num_samples;
    if((wf->filepos + bytes_needed) >= (wf->data_start + wf->data_size)) {
        bytes_needed = (uint32_t)((wf->data_start + wf->data_size) - wf->filepos);
        num_samples = bytes_needed / wf->block_align;
    }
    if(num_samples <= 0) return 0;

    // allocate temporary buffer for raw input data
    buffer = calloc(bytes_needed, 1);

    // read raw audio samples from input stream into temporary buffer
    nr = fread(buffer, wf->block_align, num_samples, wf->fp);
    br = nr * wf->block_align;
    wf->filepos += nr * wf->block_align;
    nsmp = nr * wf->channels;
    bps = wf->block_align / wf->channels;

    // do any necessary conversion based on source_format and read_format
    // also do byte swapping on big-endian systems since wave data is always
    // in little-endian order.
    if(bps == 1) {
        if(wf->source_format != WAV_SAMPLE_FMT_U8) return -1;
        fmt_convert(wf->read_format, output, wf->source_format, buffer, nsmp);
    } else if(bps == 2) {
        int16_t *input = (int16_t *)buffer;
#ifdef WORDS_BIGENDIAN
        uint16_t *buf16 = (uint16_t *)buffer;
        for(i=0; i<nsmp; i++) {
            buf16[i] = bswap_16(buf16[i]);
        }
#endif
        if(wf->source_format != WAV_SAMPLE_FMT_S16) return -1;
        fmt_convert(wf->read_format, output, wf->source_format, input, nsmp);
    } else if(bps == 3) {
        int32_t *input = calloc(nsmp, sizeof(int32_t));
        for(i=0,j=0; i<nsmp*bps; i+=bps,j++) {
            v = buffer[i] + (buffer[i+1] << 8) + (buffer[i+2] << 16);
            if(wf->bit_width == 20) {
                if(v >= (1<<19)) v -= (1<<20);
            } else if(wf->bit_width == 24) {
                if(v >= (1<<23)) v -= (1<<24);
            } else {
                fprintf(stderr, "unsupported bit width: %d\n", wf->bit_width);
                return -1;
            }
            input[j] = v;
        }
        if(wf->source_format != WAV_SAMPLE_FMT_S20 &&
                wf->source_format != WAV_SAMPLE_FMT_S24) {
            return -1;
        }
        fmt_convert(wf->read_format, output, wf->source_format, input, nsmp);
        free(input);
    } else if(bps == 4) {
        if(wf->format == WAVE_FORMAT_IEEEFLOAT) {
            float *input = (float *)buffer;
#ifdef WORDS_BIGENDIAN
            uint32_t *buf32 = (uint32_t *)buffer;
            for(i=0; i<nsmp; i++) {
                buf32[i] = bswap_32(buf32[i]);
            }
#endif
            if(wf->source_format != WAV_SAMPLE_FMT_FLT) return -1;
            fmt_convert(wf->read_format, output, wf->source_format, input, nsmp);
        } else {
            // TODO: I'm sure this can be optimized...
            int64_t v64;
            int32_t *input = calloc(nsmp, sizeof(int32_t));
            for(i=0,j=0; i<nsmp*bps; i+=bps,j++) {
                v64 = buffer[i] + (buffer[i+1] << 8) + (buffer[i+2] << 16) +
                      (((uint32_t)buffer[i+3]) << 24);
                if(v64 >= (1LL<<31)) v64 -= (1LL<<32);
                input[j] = (int32_t)v64;
            }
            if(wf->source_format != WAV_SAMPLE_FMT_S32) return -1;
            fmt_convert(wf->read_format, output, wf->source_format, input, nsmp);
            free(input);
        }
    } else if(wf->format == WAVE_FORMAT_IEEEFLOAT && bps == 8) {
        double *input = (double *)buffer;
#ifdef WORDS_BIGENDIAN
        uint64_t *buf64 = (uint64_t *)buffer;
        for(i=0; i<nsmp; i++) {
            buf64[i] = bswap_64(buf64[i]);
        }
#endif
        if(wf->source_format != WAV_SAMPLE_FMT_DBL) return -1;
        fmt_convert(wf->read_format, output, wf->source_format, input, nsmp);
    }

    // free temporary buffer
    free(buffer);

    return nr;
}

/**
 * Seeks to sample offset.
 * Syntax works like fseek. use WAV_SEEK_SET, WAV_SEEK_CUR, or WAV_SEEK_END
 * for the whence value.  Returns -1 on error, 0 otherwise.
 */
int
wavfile_seek_samples(WavFile *wf, int64_t offset, int whence)
{
    int64_t byte_offset;
    uint64_t newpos, fpos, dst, dsz;

    if(wf == NULL || wf->fp == NULL) return -1;
    if(wf->block_align <= 0) return -1;
    if(wf->filepos < wf->data_start) return -1;
    if(wf->data_size == 0) return 0;

    fpos = wf->filepos;
    dst = wf->data_start;
    dsz = wf->data_size;
    byte_offset = offset;
    byte_offset *= wf->block_align;

    // calculate new destination within file
    switch(whence) {
        case WAV_SEEK_SET:
            newpos = dst + CLIP(byte_offset, 0, (int64_t)dsz);
            break;
        case WAV_SEEK_CUR:
            newpos = fpos - MIN(-byte_offset, (int64_t)(fpos - dst));
            newpos = MIN(newpos, dst + dsz);
            break;
        case WAV_SEEK_END:
            newpos = dst + dsz - CLIP(byte_offset, 0, (int64_t)dsz);
            break;
        default: return -1;
    }

    // seek to the destination point
    if(aft_seek_set(wf, newpos)) return -1;

    return 0;
}

/**
 * Seeks to time offset, in milliseconds, based on the audio sample rate.
 * Syntax works like fseek. use WAV_SEEK_SET, WAV_SEEK_CUR, or WAV_SEEK_END
 * for the whence value.  Returns -1 on error, 0 otherwise.
 */
int
wavfile_seek_time_ms(WavFile *wf, int64_t offset, int whence)
{
    int64_t samples;
    if(wf == NULL) return -1;
    samples = offset * wf->sample_rate / 1000;
    return wavfile_seek_samples(wf, samples, whence);
}

/**
 * Returns the current stream position, in samples.
 * Returns -1 on error.
 */
uint64_t
wavfile_position(WavFile *wf)
{
    uint64_t cur;

    if(wf == NULL) return -1;
    if(wf->block_align <= 0) return -1;
    if(wf->data_start == 0 || wf->data_size == 0) return 0;

    cur = (wf->filepos - wf->data_start) / wf->block_align;
    return cur;
}

/**
 * Returns the current stream position, in milliseconds.
 * Returns -1 on error.
 */
uint64_t
wavfile_position_time_ms(WavFile *wf)
{
    return (wavfile_position(wf) * 1000 / wf->sample_rate);
}

/**
 * Prints out a description of the wav format to the specified
 * output stream.
 */
void
wavfile_print(FILE *st, WavFile *wf)
{
    char *type, *chan;
    if(st == NULL || wf == NULL) return;
    if(wf->format == WAVE_FORMAT_PCM) {
        if(wf->bit_width > 8) type = "Signed";
        else type = "Unsigned";
    } else if(wf->format == WAVE_FORMAT_IEEEFLOAT) {
        type = "Floating-point";
    } else {
        type = "[unsupported type]";
    }
    switch(wf->channels) {
        case 1: chan = "mono"; break;
        case 2: chan = "stereo"; break;
        case 3: chan = "3-channel"; break;
        case 4: chan = "4-channel"; break;
        case 5: chan = "5-channel"; break;
        case 6: chan = "6-channel"; break;
        default: chan = "multi-channel"; break;
    }
    fprintf(st, "%s %d-bit %d Hz %s\n", type, wf->bit_width, wf->sample_rate,
            chan);
}
