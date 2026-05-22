#include "lz77.h"

#include <string.h>
#include <stdio.h>

/* -------------------------------------------------------------------------
 * CONCEPTOS CLAVE (para entender el código antes de leerlo)
 *
 * LZ77 escanea el input de izquierda a derecha manteniendo dos regiones:
 *
 *   [....... SLIDING WINDOW .......][.. LOOKAHEAD ..][ input sin ver ]
 *                                   ^
 *                                 src_pos
 *
 * SLIDING WINDOW (4096 B): los bytes ya procesados, guardados en el mismo
 *   buffer de entrada. Usamos el buffer original — no hay copia extra.
 *
 * LOOKAHEAD (16 B): los bytes que queremos comprimir ahora. Buscamos si
 *   alguna subcadena de la ventana coincide con el prefijo del lookahead.
 *
 * TOKEN DE SALIDA (3 bytes fijos):
 *   - offset: distancia hacia atrás donde empieza el match (12 bits → 0..4095)
 *   - length: cuántos bytes coinciden (4 bits → 0..15)
 *   - literal: el byte que sigue al match (siempre presente)
 *
 * Un token cubre (length + 1) bytes de entrada y siempre produce 3 bytes.
 * Cuando length = 0, es un "token literal puro": solo se emite el byte actual.
 * -------------------------------------------------------------------------
 */

/* -------------------------------------------------------------------------
 * find_longest_match — busca en la ventana el match más largo para src[pos].
 *
 * Complejidad: O(WINDOW × LOOKAHEAD) por llamada — O(n) total para el archivo.
 * En producción se usarían hash tables; aquí la claridad es más importante.
 *
 * NOTA sobre matches solapados:
 *   Si la ventana termina en "abc" y el lookahead es "abcabc", el match puede
 *   ser de longitud 6 aunque solo hay 3 bytes en la ventana. Esto es válido:
 *   el decompressor copia byte a byte, extendiendo el patrón hacia adelante.
 *   Ejemplo clásico: un run de ceros puede comprimirse como offset=1, length=N.
 * -------------------------------------------------------------------------
 */
static int find_longest_match(const uint8_t *src, size_t src_len,
                               size_t pos, uint16_t *out_offset)
{
    /* Inicio de la ventana: máximo WINDOW_SIZE bytes hacia atrás */
    size_t win_start = (pos >= LZ77_WINDOW_SIZE) ? pos - LZ77_WINDOW_SIZE : 0;

    /*
     * Necesitamos al menos 1 byte después del match para el literal.
     * Si solo queda 1 byte en el input, no podemos hacer match: emitimos literal.
     */
    size_t remaining = src_len - pos;
    if (remaining <= 1) {
        *out_offset = 0;
        return 0;
    }

    /* Longitud máxima del match: min(LOOKAHEAD, remaining - 1) */
    size_t max_len = (remaining - 1 < LZ77_LOOKAHEAD_SIZE)
                     ? (remaining - 1) : LZ77_LOOKAHEAD_SIZE;

    int      best_len = 0;
    uint16_t best_off = 0;

    /*
     * Escaneo lineal de la ventana. Para cada posición candidata i:
     *   - src[i .. i+m-1] debe coincidir con src[pos .. pos+m-1]
     *   - i + m puede entrar en la zona del lookahead (match solapado — OK)
     *   - La cota pos + max_len <= src_len garantiza que src[pos+m] es válido
     */
    for (size_t i = win_start; i < pos; i++) {
        size_t m = 0;
        while (m < max_len && src[i + m] == src[pos + m]) {
            m++;
        }
        if ((int)m > best_len) {
            best_len = (int)m;
            best_off = (uint16_t)(pos - i);
        }
    }

    *out_offset = best_off;
    return best_len;
}

/* -------------------------------------------------------------------------
 * write_token — serializa un token LZ77 en exactamente 3 bytes.
 *
 * Layout de bits en el wire:
 *
 *   Byte 0: [ O11 O10 O09 O08 O07 O06 O05 O04 ]   bits 11..4 del offset
 *   Byte 1: [ O03 O02 O01 O00 L03 L02 L01 L00 ]   bits 3..0 del offset + length
 *   Byte 2: [ literal byte                    ]
 *
 * Ejemplo: offset=0xABC (1010 1011 1100), length=0xD (1101), literal=0xEF
 *   Byte 0 = 0xAB  (0xABC >> 4)
 *   Byte 1 = 0xCD  ((0xC << 4) | 0xD)
 *   Byte 2 = 0xEF
 * -------------------------------------------------------------------------
 */
static void write_token(uint8_t *dst,
                        uint16_t offset, uint8_t length, uint8_t literal)
{
    dst[0] = (uint8_t)(offset >> 4);
    dst[1] = (uint8_t)((offset & 0x0F) << 4) | (length & 0x0F);
    dst[2] = literal;
}

/* -------------------------------------------------------------------------
 * lz77_compress — compresor LZ77 con ventana deslizante de 4096 bytes.
 *
 * Pipeline de decisión por iteración:
 *
 *   1. Busca el match más largo en la ventana para src[src_pos]
 *   2a. Si length >= 1: emite (offset, length, src[src_pos + length])
 *       y avanza src_pos += length + 1
 *   2b. Si length == 0: emite (0, 0, src[src_pos]) y avanza src_pos += 1
 *
 * ¿Por qué cualquier match (length >= 1) vale la pena?
 *   - 1 literal-only token = 3 bytes de salida para 1 byte de entrada
 *   - 1 match de length=1  = 3 bytes de salida para 2 bytes de entrada
 *   → Siempre es mejor o igual emitir el match cuando existe.
 *
 * Peor caso (sin matches): src_len × 3 bytes de salida.
 * Mejor caso (todo repetido): ~3 bytes de salida por bloque de 16 bytes.
 * -------------------------------------------------------------------------
 */
ssize_t lz77_compress(const uint8_t *src, size_t src_len,
                      uint8_t *dst, size_t dst_len)
{
    if (!src || !dst || src_len == 0) return 0;

    /* Reservar espacio para el peor caso: cada byte se convierte en un token */
    if (dst_len < src_len * LZ77_TOKEN_BYTES) {
        fprintf(stderr, "lz77_compress: dst buffer too small "
                "(%zu needed, %zu available)\n",
                src_len * LZ77_TOKEN_BYTES, dst_len);
        return -1;
    }

    size_t src_pos = 0;
    size_t dst_pos = 0;

    while (src_pos < src_len) {
        uint16_t offset = 0;
        int      length = find_longest_match(src, src_len, src_pos, &offset);

        uint8_t literal;
        if (length >= 1) {
            /* Match encontrado: el literal es el byte inmediatamente después */
            literal = src[src_pos + length];
        } else {
            /* Sin match: emitir el byte actual como literal puro */
            length  = 0;
            offset  = 0;
            literal = src[src_pos];
        }

        write_token(dst + dst_pos, offset, (uint8_t)length, literal);
        dst_pos += LZ77_TOKEN_BYTES;
        src_pos += (size_t)length + 1;  /* avanzar: match + literal */
    }

    return (ssize_t)dst_pos;
}

/* -------------------------------------------------------------------------
 * lz77_decompress — decodifica un stream de tokens LZ77 al dato original.
 *
 * Por cada token de 3 bytes:
 *   1. Extrae offset (12 bits), length (4 bits), literal (8 bits)
 *   2. Si length > 0: copia `length` bytes desde out[out_pos - offset]
 *      → copia byte a byte para soportar matches solapados (overlapping).
 *        memcpy no sirve aquí: no garantiza orden, rompería el patrón.
 *   3. Añade el literal byte al final.
 *
 * Invariante: la entrada debe ser múltiplo de LZ77_TOKEN_BYTES (3 bytes).
 * Returns bytes escritos en dst, o -1 en error.
 * -------------------------------------------------------------------------
 */
ssize_t lz77_decompress(const uint8_t *src, size_t src_len,
                        uint8_t *dst, size_t dst_len)
{
    if (!src || !dst || src_len == 0) return 0;

    if (src_len % LZ77_TOKEN_BYTES != 0) {
        fprintf(stderr, "lz77_decompress: src length (%zu) not a multiple of %d\n",
                src_len, LZ77_TOKEN_BYTES);
        return -1;
    }

    size_t src_pos = 0;
    size_t dst_pos = 0;

    while (src_pos < src_len) {
        /* Deserializar 3 bytes → offset (12b), length (4b), literal (8b) */
        uint8_t  b0 = src[src_pos];
        uint8_t  b1 = src[src_pos + 1];
        uint8_t  b2 = src[src_pos + 2];
        src_pos += LZ77_TOKEN_BYTES;

        uint16_t offset  = ((uint16_t)b0 << 4) | (b1 >> 4);
        uint8_t  length  = b1 & 0x0F;
        uint8_t  literal = b2;

        /* Fase 1: copiar `length` bytes del back-reference */
        if (length > 0) {
            if (dst_pos < (size_t)offset) {
                fprintf(stderr,
                        "lz77_decompress: back-reference offset=%u exceeds "
                        "output position %zu\n", offset, dst_pos);
                return -1;
            }
            size_t match_start = dst_pos - offset;
            for (uint8_t m = 0; m < length; m++) {
                if (dst_pos >= dst_len) {
                    fprintf(stderr, "lz77_decompress: dst buffer overflow\n");
                    return -1;
                }
                /*
                 * Copia byte a byte — no memcpy.
                 * Cuando match_start + m >= dst_pos original, estamos leyendo
                 * bytes que acabamos de escribir: eso es intencional y produce
                 * el efecto "run" de los matches solapados.
                 */
                dst[dst_pos++] = dst[match_start + m];
            }
        }

        /* Fase 2: escribir el literal byte */
        if (dst_pos >= dst_len) {
            fprintf(stderr, "lz77_decompress: dst buffer overflow on literal\n");
            return -1;
        }
        dst[dst_pos++] = literal;
    }

    return (ssize_t)dst_pos;
}
