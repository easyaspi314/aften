/**
 * Audio filter routines
 * Copyright (c) 2006  Justin Ruggles <jruggle@eathlink.net>
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

#ifndef FILTER_H
#define FILTER_H

enum FilterType {
    FILTER_TYPE_LOWPASS,
    FILTER_TYPE_HIGHPASS,
    FILTER_TYPE_BANDPASS,
    FILTER_TYPE_BANDSTOP,
    FILTER_TYPE_ALLPASS,
};

enum FilterID {
    FILTER_ID_BIQUAD_I,
    FILTER_ID_BIQUAD_II,
    FILTER_ID_BUTTERWORTH_I,
    FILTER_ID_BUTTERWORTH_II,
    FILTER_ID_ONEPOLE,
};

struct Filter;

typedef struct {
    struct Filter *filter;
    void *private;
    enum FilterType type;
    int cascaded;
    double cutoff;
    double cutoff2;
    double samplerate;
    int taps;
} FilterContext;

extern int filter_init(FilterContext *f, enum FilterID id);

extern void filter_run(FilterContext *f, double *out, double *in, int n);

extern void filter_close(FilterContext *f);

#endif /* FILTER_H */
