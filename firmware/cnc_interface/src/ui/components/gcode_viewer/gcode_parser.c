/**
 * @file gcode_parser.c
 * @brief Fast, zero-allocation G-code line parser.
 *
 * Single-pass hand-written scanner.  No malloc, no string copies.
 * Designed for ESP32-S3 where every microsecond and byte counts.
 */

#include "gcode_parser.h"
#include <ctype.h>
#include <math.h>
#include <string.h>

/* ─── internal helpers ─────────────────────────────────────────────────── */

/** Advance *p past whitespace (spaces and tabs only). */
static inline void skip_ws(const char **p) {
    while (**p == ' ' || **p == '\t') (*p)++;
}

/** Parse a decimal number (optional sign, integer + optional fraction).
 *  Advances *p past the number.  Returns the value. */
static float parse_float(const char **p) {
    float sign = 1.0f;
    if (**p == '-')      { sign = -1.0f; (*p)++; }
    else if (**p == '+') { (*p)++; }

    float val  = 0.0f;
    /* Integer part */
    while (**p >= '0' && **p <= '9') {
        val = val * 10.0f + (float)(**p - '0');
        (*p)++;
    }
    /* Fractional part */
    if (**p == '.') {
        (*p)++;
        float frac = 0.1f;
        while (**p >= '0' && **p <= '9') {
            val += (float)(**p - '0') * frac;
            frac *= 0.1f;
            (*p)++;
        }
    }
    return sign * val;
}

/** Parse an integer (optional sign).  Advances *p. */
static int parse_int(const char **p) {
    int sign = 1;
    if (**p == '-')      { sign = -1; (*p)++; }
    else if (**p == '+') { (*p)++; }

    int val = 0;
    while (**p >= '0' && **p <= '9') {
        val = val * 10 + (**p - '0');
        (*p)++;
    }
    return sign * val;
}

/* ─── public API ───────────────────────────────────────────────────────── */

bool gcode_parse_line(const char *line, gc_parsed_line_t *out) {
    if (!line || !out) return false;

    /* Initialise output — NAN means "not present" for floats. */
    out->g = NAN;
    out->m = NAN;
    out->x = NAN;  out->y = NAN;  out->z = NAN;
    out->i = NAN;  out->j = NAN;  out->k = NAN;
    out->f = NAN;  out->s = NAN;
    out->p = NAN;  out->r = NAN;
    out->t = -1;   out->n = -1;
    out->words = 0;

    const char *p = line;
    bool found_any = false;

    while (*p && *p != '\n' && *p != '\r') {
        skip_ws(&p);
        if (*p == '\0' || *p == '\n' || *p == '\r') break;

        /* Comment — skip parenthesised text */
        if (*p == '(') {
            while (*p && *p != ')') p++;
            if (*p == ')') p++;
            continue;
        }

        /* ; comment — rest of line is comment */
        if (*p == ';') break;

        /* Checksum — ignore *nn at end */
        if (*p == '*') break;

        /* % — program delimiter, skip */
        if (*p == '%') { p++; continue; }

        /* Expect a letter word */
        char letter = (char)toupper((unsigned char)*p);
        if (letter < 'A' || letter > 'Z') {
            /* Unknown character — skip it */
            p++;
            continue;
        }

        p++;  /* advance past the letter */
        skip_ws(&p);

        /* Parse the numeric value */
        switch (letter) {
        case 'G': out->g = parse_float(&p); out->words |= GCW_G; found_any = true; break;
        case 'M': out->m = parse_float(&p); out->words |= GCW_M; found_any = true; break;
        case 'X': out->x = parse_float(&p); out->words |= GCW_X; found_any = true; break;
        case 'Y': out->y = parse_float(&p); out->words |= GCW_Y; found_any = true; break;
        case 'Z': out->z = parse_float(&p); out->words |= GCW_Z; found_any = true; break;
        case 'I': out->i = parse_float(&p); out->words |= GCW_I; found_any = true; break;
        case 'J': out->j = parse_float(&p); out->words |= GCW_J; found_any = true; break;
        case 'K': out->k = parse_float(&p); out->words |= GCW_K; found_any = true; break;
        case 'F': out->f = parse_float(&p); out->words |= GCW_F; found_any = true; break;
        case 'S': out->s = parse_float(&p); out->words |= GCW_S; found_any = true; break;
        case 'P': out->p = parse_float(&p); out->words |= GCW_P; found_any = true; break;
        case 'R': out->r = parse_float(&p); out->words |= GCW_R; found_any = true; break;
        case 'T': out->t = parse_int(&p);   out->words |= GCW_T; found_any = true; break;
        case 'N': out->n = parse_int(&p);   out->words |= GCW_N; found_any = true; break;
        default:
            /* Unknown word letter — skip its value */
            parse_float(&p);
            break;
        }
    }

    return found_any;
}

int gcode_parse_block(const char *text, gc_parse_line_cb_t cb, void *ctx) {
    if (!text || !cb) return 0;

    int count = 0;
    const char *p = text;

    while (*p) {
        /* Find end of current line */
        const char *eol = p;
        while (*eol && *eol != '\n') eol++;

        /* Only parse if line has content */
        if (eol > p) {
            /* We need a NUL-terminated line.  Since the input might not
             * have one at `eol`, use a small stack buffer. */
            size_t len = (size_t)(eol - p);
            if (len > GCVIEW_MAX_LINE_LEN) len = GCVIEW_MAX_LINE_LEN;

            char buf[GCVIEW_MAX_LINE_LEN + 1];
            memcpy(buf, p, len);
            buf[len] = '\0';

            gc_parsed_line_t parsed;
            if (gcode_parse_line(buf, &parsed)) {
                count++;
                if (!cb(&parsed, ctx)) break;   /* early exit */
            }
        }

        /* Advance past this line */
        p = eol;
        if (*p == '\n') p++;
    }

    return count;
}
