#ifndef FFTSG_H
#define FFTSG_H
#include "nextpnr_namespaces.h"

NEXTPNR_NAMESPACE_BEGIN

//
// The following FFT library came from
// http://www.kurims.kyoto-u.ac.jp/~ooura/fft.html
//
//
/// 1D FFT ////////////////////////////////////////////////////////////////
void ddct(int n, int isgn, float *a, int *ip, float *w);
void ddst(int n, int isgn, float *a, int *ip, float *w);

/// 2D FFT ////////////////////////////////////////////////////////////////
void ddct2d(int n1, int n2, int isgn, float **a, float *t, int *ip, float *w);
void ddsct2d(int n1, int n2, int isgn, float **a, float *t, int *ip, float *w);
void ddcst2d(int n1, int n2, int isgn, float **a, float *t, int *ip, float *w);


NEXTPNR_NAMESPACE_END

#endif
