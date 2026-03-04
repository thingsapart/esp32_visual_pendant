/**
 * @file gcode_parser.h
 * @brief Fast, zero-allocation G-code line parser.
 *
 * Parses a single ASCII G-code line into a `gc_parsed_line_t` struct.
 * No heap allocation — works entirely on the caller-supplied struct.
 *
 * Supports:
 *  - Standard single-letter words: G, M, X, Y, Z, I, J, K, F, S, P, R, T, N
 *  - Comments in parentheses `(…)` and semicolons `;…`
 *  - Leading/trailing whitespace
 *  - Checksums `*nn` (ignored)
 *  - Case-insensitive word letters
 */
#ifndef GCODE_PARSER_H
#define GCODE_PARSER_H

#include "gcode_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Parse a single G-code line into @p out.
 *
 * @param line   NUL-terminated ASCII line (may include newline / CR).
 * @param out    Output struct — all word fields initialised to NAN / -1,
 *               `words` bitmask set for each word found.
 * @return       true if at least one word was parsed, false if the line
 *               is empty / comment-only.
 */
bool gcode_parse_line(const char *line, gc_parsed_line_t *out);

/**
 * Parse a block of multiple G-code lines separated by '\\n'.
 *
 * Calls @p cb for each successfully parsed line.  Stops early if @p cb
 * returns false.
 *
 * @param text   NUL-terminated text (may contain multiple lines).
 * @param cb     Callback invoked for each parsed line.
 * @param ctx    Opaque context passed to @p cb.
 * @return       Number of lines successfully parsed.
 */
typedef bool (*gc_parse_line_cb_t)(const gc_parsed_line_t *line, void *ctx);

int gcode_parse_block(const char *text, gc_parse_line_cb_t cb, void *ctx);

#ifdef __cplusplus
}
#endif

#endif /* GCODE_PARSER_H */
