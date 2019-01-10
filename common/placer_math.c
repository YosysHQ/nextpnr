#include "taucs.h"
#include "placer_math.h"
#include <stdio.h>

void taucif_init_solver() {
    taucs_logfile("stdout");
}

struct taucif_system {
    taucs_ccs_matrix* mat;
    int ccs_i, ccs_col;
};

struct taucif_system *taucif_create_system(int rows, int cols, int n_nonzero) {
    struct taucif_system *sys = taucs_malloc(sizeof(struct taucif_system));
    sys->mat = taucs_ccs_create(cols, rows, n_nonzero, TAUCS_DOUBLE | TAUCS_SYMMETRIC);
    // Internal pointers
    sys->ccs_i = 0;
    sys->ccs_col = -1;
    return sys;
};

void taucif_set_matrix_value(struct taucif_system *sys, int row, int col, double value) {
    while(sys->ccs_col < col) {
        sys->mat->colptr[++sys->ccs_col] = sys->ccs_i;
    }
    sys->mat->rowind[sys->ccs_i] = row;
    sys->mat->values.d[sys->ccs_i++] = value;
}

void taucif_solve_system(struct taucif_system *sys, double *x, double *rhs) {
    // FIXME: preconditioner, droptol??
    taucs_ccs_matrix* precond_mat = taucs_ccs_factor_llt(sys->mat, 1e-3, 0);
    // FIXME: itermax, convergetol
    int cjres = taucs_conjugate_gradients(sys->mat, taucs_ccs_solve_llt, precond_mat, x, rhs, 1000, 1e-6);
    taucs_ccs_free(precond_mat);
}

void taucif_free_system(struct taucif_system *sys) {
    taucs_ccs_free(sys->mat);
    taucs_free(sys->mat);
}

