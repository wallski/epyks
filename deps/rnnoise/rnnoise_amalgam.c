/*
 * RNNoise amalgamation unit — compiled as C (not C++) by MSVC.
 * This file includes all RNNoise source files in the correct dependency order
 * so they compile into a single translation unit with no DLL requirements.
 *
 * License: BSD (see individual files)
 */

/* Suppress MSVC-specific warnings that don't apply to this C library */
#ifdef _MSC_VER
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include <malloc.h>
#pragma warning(push)
#pragma warning(disable: 4244)  /* possible loss of data (float/double conversions) */
#pragma warning(disable: 4305)  /* truncation from double to float */
#pragma warning(disable: 4996)  /* deprecated POSIX names */
#pragma warning(disable: 4267)  /* possible loss of data (size_t to int) */
#pragma warning(disable: 4101)  /* unreferenced local variable */
#endif

/* The path is relative to this file's location in deps/rnnoise/ */
#include "kiss_fft.c"
#include "celt_lpc.c"
#include "pitch.c"
#include "rnn.c"
#include "rnn_data.c"
#include "denoise.c"

#ifdef _MSC_VER
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include <malloc.h>
#pragma warning(pop)
#endif

