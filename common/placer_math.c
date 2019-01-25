#include "taucs.h"
#include "placer_math.h"
#include <stdio.h>
#include <assert.h>

void taucif_init_solver() {
    //taucs_logfile("stdout");
}

struct taucif_system {
    taucs_ccs_matrix* mat;
    int ccs_i, ccs_col;
};

struct taucif_system *taucif_create_system(int rows, int cols, int n_nonzero) {
    struct taucif_system *sys = taucs_malloc(sizeof(struct taucif_system));
    sys->mat = taucs_ccs_create(cols, rows, n_nonzero, TAUCS_DOUBLE | TAUCS_SYMMETRIC | TAUCS_LOWER);
    // Internal pointers
    sys->ccs_i = 0;
    sys->ccs_col = -1;
    return sys;
};

void taucif_add_matrix_value(struct taucif_system *sys, int row, int col, double value) {
    assert(sys->ccs_col <= col);
    while(sys->ccs_col < col) {
        sys->mat->colptr[++sys->ccs_col] = sys->ccs_i;
    }
    sys->mat->rowind[sys->ccs_i] = row;
    sys->mat->values.d[sys->ccs_i++] = value;
}

void taucif_finalise_matrix(struct taucif_system *sys) {
    sys->mat->colptr[++sys->ccs_col] = sys->ccs_i;
#if 0
    taucs_ccs_write_ijv(sys->mat, "matrix.ijv");
#endif
}

int taucif_solve_system(struct taucif_system *sys, double *x, double *rhs) {
    // FIXME: preconditioner, droptol??
    taucs_ccs_matrix* precond_mat = taucs_ccs_factor_llt(sys->mat, 1e-2, 0);
    if (precond_mat == NULL)
        return -1;
    // FIXME: itermax, convergetol
    int cjres = taucs_conjugate_gradients(sys->mat, taucs_ccs_solve_llt, precond_mat, x, rhs, 1000, 1e-6);
    taucs_ccs_free(precond_mat);
    return 0;
}

void taucif_free_system(struct taucif_system *sys) {
    taucs_ccs_free(sys->mat);
    taucs_free(sys);
}

