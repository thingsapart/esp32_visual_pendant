#include "touch_calib.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <Preferences.h>

// Storage key constants
static const char *PREF_NAMESPACE = "touchcal";
static const char *PREF_KEY_H = "homography";

static bool _loaded = false;
static float _H[9] = { 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0 };

static bool read_h_from_prefs(float H[9]) {
    Preferences p;
    if (!p.begin(PREF_NAMESPACE, true)) return false;
    size_t sz = p.getBytesLength(PREF_KEY_H);
    if (sz != sizeof(float) * 9) { p.end(); return false; }
    p.getBytes(PREF_KEY_H, H, sizeof(float) * 9);
    p.end();
    return true;
}

bool touch_calib_has(void) {
    Preferences p;
    if (!p.begin(PREF_NAMESPACE, true)) return false;
    size_t sz = p.getBytesLength(PREF_KEY_H);
    p.end();
    return (sz == sizeof(float) * 9);
}

bool touch_calib_load_to(float H[9]) {
    return (_loaded = read_h_from_prefs(H));
}

bool touch_calib_apply(const float H[9]) {
    for (size_t i = 0; i < 9; ++i) {
        _H[i] = H[i];
    }
    _loaded = true;
    return true;
}

bool touch_calib_load() {
    return (_loaded = read_h_from_prefs(_H));
}

bool touch_calib_save(const float H[9]) {
    Preferences p;
    if (!p.begin(PREF_NAMESPACE, false)) return false;
    bool ok = p.putBytes(PREF_KEY_H, H, sizeof(float) * 9) == sizeof(float) * 9;
    p.end();
    return ok;
}

void touch_calib_clear(void) {
    Preferences p;
    if (!p.begin(PREF_NAMESPACE, false)) return;
    p.remove(PREF_KEY_H);
    p.end();
}

// Apply homography H to (x,y) in place. H is row-major 3x3
void touch_calib_apply_inplace(float *x, float *y) {
    // No-op, aka 1:1 mapping if calibration not loaded.
    if (!_loaded) { return; }

    float X = *x, Y = *y;
    float nx = _H[0]*X + _H[1]*Y + _H[2];
    float ny = _H[3]*X + _H[4]*Y + _H[5];
    float nw = _H[6]*X + _H[7]*Y + _H[8];
    if (nw != 0.0f) {
        nx /= nw;
        ny /= nw;
    }
    *x = nx; *y = ny;
}

// Simple Gaussian elimination linear solver for Ax=b (n x n). Returns false on singular.
static bool solve_linear(int n, float A[][8], float b[], float x[]) {
    float aug[8][9];
    if (n > 8) return false;
    for (int i=0;i<n;i++) {
        for (int j=0;j<n;j++) aug[i][j] = A[i][j];
        aug[i][n] = b[i];
    }
    for (int i=0;i<n;i++) {
        int piv = i;
        for (int r=i+1;r<n;r++) if (fabsf(aug[r][i]) > fabsf(aug[piv][i])) piv = r;
        if (fabsf(aug[piv][i]) < 1e-8f) return false;
        if (piv != i) for (int c=i;c<=n;c++) { float t = aug[i][c]; aug[i][c] = aug[piv][c]; aug[piv][c] = t; }
        float div = aug[i][i];
        for (int c=i;c<=n;c++) aug[i][c] /= div;
        for (int r=0;r<n;r++) if (r!=i) {
            float m = aug[r][i];
            for (int c=i;c<=n;c++) aug[r][c] -= m * aug[i][c];
        }
    }
    for (int i=0;i<n;i++) x[i] = aug[i][n];
    return true;
}

// Compute homography using DLT for 4 point correspondences. Solves for 8 unknowns.
bool touch_calib_compute_homography(const float src[4][2], const float dst[4][2], float H_out[9]) {
    float A[8][8];
    float b[8];
    memset(A, 0, sizeof(A));
    memset(b, 0, sizeof(b));
    for (int i=0;i<4;i++) {
        float x = src[i][0];
        float y = src[i][1];
        float u = dst[i][0];
        float v = dst[i][1];
        A[i*2 + 0][0] = x; A[i*2 + 0][1] = y; A[i*2 + 0][2] = 1; A[i*2 + 0][3] = 0; A[i*2 + 0][4] = 0; A[i*2 + 0][5] = 0; A[i*2 + 0][6] = -u * x; A[i*2 + 0][7] = -u * y;
        b[i*2 + 0] = u;

        A[i*2 + 1][0] = 0; A[i*2 + 1][1] = 0; A[i*2 + 1][2] = 0; A[i*2 + 1][3] = x; A[i*2 + 1][4] = y; A[i*2 + 1][5] = 1; A[i*2 + 1][6] = -v * x; A[i*2 + 1][7] = -v * y;
        b[i*2 + 1] = v;
    }

    float xsol[8];
    if (!solve_linear(8, A, b, xsol)) return false;
    H_out[0] = xsol[0]; H_out[1] = xsol[1]; H_out[2] = xsol[2];
    H_out[3] = xsol[3]; H_out[4] = xsol[4]; H_out[5] = xsol[5];
    H_out[6] = xsol[6]; H_out[7] = xsol[7]; H_out[8] = 1.0f;
    return true;
}
