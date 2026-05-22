#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "io.h"
#include "lz77.h"

/*
 * Formato del archivo comprimido (.lz77 / .ite):
 *   Bytes 0-3: magic "LZ77"        — identifica el formato, evita parsear basura
 *   Bytes 4-7: original_size       — uint32_t little-endian, para malloc exacto
 *   Bytes 8+:  stream de tokens    — salida cruda de lz77_compress()
 *
 * El header nos da el tamaño original sin tener que leer todo el stream primero.
 * Es el mismo principio que el header de gzip (ID1/ID2 + ISIZE).
 */
static const uint8_t MAGIC[4] = { 'L', 'Z', '7', '7' };
#define HDR_SIZE 8  /* 4 bytes magic + 4 bytes uint32_t */

static void usage(const char *prog) {
    fprintf(stderr,
        "Uso:\n"
        "  %s -c <entrada> <salida>   Comprimir con LZ77\n"
        "  %s -d <entrada> <salida>   Descomprimir LZ77\n"
        "  %s -e <entrada> <salida>   Comprimir + Encriptar (Commit 6)\n",
        prog, prog, prog);
    exit(1);
}

/* -------------------------------------------------------------------------
 * do_compress — lee el archivo, comprime en RAM, escribe resultado con header.
 *
 * Flujo:
 *   read_file() → buffer completo en RAM
 *   lz77_compress() → buffer comprimido en RAM (sin I/O)
 *   write_file() → UNA llamada write() al disco
 * -------------------------------------------------------------------------
 */
static int do_compress(const char *in_path, const char *out_path) {
    uint8_t *src = NULL;
    size_t   src_len = 0;

    if (read_file(in_path, &src, &src_len) < 0)
        return 1;

    /* Reservar peor caso: cada byte → 1 token de 3 bytes */
    size_t   comp_cap = src_len * LZ77_TOKEN_BYTES + HDR_SIZE;
    uint8_t *out_buf  = malloc(comp_cap);
    if (!out_buf) {
        perror("do_compress: malloc");
        free(src);
        return 1;
    }

    /* Comprimir a partir del byte HDR_SIZE (dejamos espacio para el header) */
    ssize_t comp_len = lz77_compress(src, src_len,
                                     out_buf + HDR_SIZE,
                                     comp_cap  - HDR_SIZE);
    if (comp_len < 0) {
        free(src);
        free(out_buf);
        return 1;
    }

    /* Escribir header: magic + tamaño original */
    memcpy(out_buf, MAGIC, 4);
    uint32_t orig32 = (uint32_t)src_len;
    memcpy(out_buf + 4, &orig32, 4);  /* little-endian en x86 */

    size_t total = HDR_SIZE + (size_t)comp_len;
    if (write_file(out_path, out_buf, total) < 0) {
        free(src);
        free(out_buf);
        return 1;
    }

    fprintf(stderr, "[lz77] compress: %zu → %zd bytes (ratio %.1f%%)\n",
            src_len, comp_len, 100.0 * (double)comp_len / (double)src_len);

    free(src);
    free(out_buf);
    return 0;
}

/* -------------------------------------------------------------------------
 * do_decompress — lee el archivo comprimido, descomprime en RAM, escribe.
 *
 * Lee el header para saber el tamaño original → malloc exacto →
 * lz77_decompress() → write_file().
 * -------------------------------------------------------------------------
 */
static int do_decompress(const char *in_path, const char *out_path) {
    uint8_t *src = NULL;
    size_t   src_len = 0;

    if (read_file(in_path, &src, &src_len) < 0)
        return 1;

    /* Validar header */
    if (src_len < HDR_SIZE || memcmp(src, MAGIC, 4) != 0) {
        fprintf(stderr, "do_decompress: '%s' no es un archivo LZ77 válido\n",
                in_path);
        free(src);
        return 1;
    }

    uint32_t orig_size;
    memcpy(&orig_size, src + 4, 4);

    uint8_t *out_buf = malloc(orig_size);
    if (!out_buf) {
        perror("do_decompress: malloc");
        free(src);
        return 1;
    }

    ssize_t decomp_len = lz77_decompress(src + HDR_SIZE, src_len - HDR_SIZE,
                                         out_buf, (size_t)orig_size);
    if (decomp_len < 0) {
        free(src);
        free(out_buf);
        return 1;
    }

    if (write_file(out_path, out_buf, (size_t)decomp_len) < 0) {
        free(src);
        free(out_buf);
        return 1;
    }

    fprintf(stderr, "[lz77] decompress: %zu → %zd bytes\n",
            src_len - HDR_SIZE, decomp_len);

    free(src);
    free(out_buf);
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc != 4)
        usage(argv[0]);

    const char *mode     = argv[1];
    const char *in_path  = argv[2];
    const char *out_path = argv[3];

    if (strcmp(mode, "-c") == 0) return do_compress(in_path, out_path);
    if (strcmp(mode, "-d") == 0) return do_decompress(in_path, out_path);

    if (strcmp(mode, "-e") == 0) {
        fprintf(stderr, "[-e] pipeline completo no implementado aún (Commit 6)\n");
        return 1;
    }

    usage(argv[0]);
    return 1;
}
