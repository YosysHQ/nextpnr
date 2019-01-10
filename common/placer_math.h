/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2019  David Shah <david@symbioticeda.com>
 *
 *  Permission to use, copy, modify, and/or distribute this software for any
 *  purpose with or without fee is hereby granted, provided that the above
 *  copyright notice and this permission notice appear in all copies.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 *  WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 *  ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 *  WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 *  ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 *  OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

 */

#ifndef PLACER_MATH_H
#define PLACER_MATH_H
// This shim is needed because Tauc is mutually incompatible with modern C++ (implementing macros and functions
// that collide with max, min, etc)
#ifdef __cplusplus
extern "C" {
#endif
extern void taucif_init_solver();

struct taucif_system;

extern struct taucif_system *taucif_create_system(int rows, int cols, int n_nonzero);

extern void taucif_set_matrix_value(struct taucif_system *sys, int row, int col, double value);

extern void taucif_solve_system(struct taucif_system *sys, double *x, double *rhs);

extern void taucif_free_system(struct taucif_system *sys);

#ifdef __cplusplus
}
#endif

#endif