#ifndef LZ77_H
#define LZ77_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>  /* ssize_t */

/*
 * LZ77_WINDOW_SIZE — sliding window coincides with the x86 page size (4096 B).
 *
 * When this buffer lives in the kernel page cache, aligning the window to the
 * hardware page boundary means the OS can map it as a single TLB entry. Any
 * back-reference scan stays within that one page, avoiding TLB misses and the
 * associated MMU penalties during the inner match loop.
 */
#define LZ77_WINDOW_SIZE    4096

/*
 * LZ77_LOOKAHEAD_SIZE — maximum match length.
 *
 * Encoded in 4 bits inside the token, so max representable value is 15.
 * 16 bytes of lookahead is a sweet spot for text: most useful repetitions
 * (common words, phrases, indentation) fall within this window.
 */
#define LZ77_LOOKAHEAD_SIZE 16

/*
 * LZ77Token — internal representation of a single compressed unit.
 *
 * Wire format (3 bytes, see lz77.c):
 *   Byte 0: offset[11:4]
 *   Byte 1: offset[3:0] | length[3:0]
 *   Byte 2: literal
 *
 * A token always covers (length + 1) input bytes: `length` bytes from the
 * back-reference plus 1 literal byte. When length == 0, only the literal
 * is emitted (no back-reference).
 */
typedef struct {
    uint16_t offset;   /* distance back in the window: 0..4095 (12 bits on wire) */
    uint8_t  length;   /* match length: 0..15                (4 bits on wire)    */
    uint8_t  literal;  /* raw byte following the match        (8 bits on wire)    */
} LZ77Token;

/* Wire size of one serialized token */
#define LZ77_TOKEN_BYTES 3

/*
 * lz77_compress — compress src into dst using LZ77.
 *
 * dst must be at least (src_len * LZ77_TOKEN_BYTES) bytes — worst case when
 * no matches are found and every byte becomes a 3-byte literal token.
 *
 * Returns bytes written to dst, or -1 if dst is too small.
 */
ssize_t lz77_compress(const uint8_t *src, size_t src_len,
                      uint8_t *dst, size_t dst_len);

/*
 * lz77_decompress — decode a stream of LZ77 tokens back to the original data.
 * Declared here; implemented in Commit 3.
 *
 * Returns bytes written to dst, or -1 on error.
 */
ssize_t lz77_decompress(const uint8_t *src, size_t src_len,
                        uint8_t *dst, size_t dst_len);

#endif /* LZ77_H */
