#define _GNU_SOURCE  /* explicit_bzero */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "io.h"
#include "lz77.h"
#include "rc4.h"

/*
 * Formatos de archivo soportados:
 *
 *  "LZ77" — solo compresión (-c / -d):
 *    Bytes 0-3:  'L' 'Z' '7' '7'
 *    Bytes 4-7:  original_size  (uint32_t little-endian)
 *    Bytes 8+:   stream de tokens LZ77
 *
 *  "ITEC" — compresión + encriptación (-e / -d):
 *    Bytes 0-3:  'I' 'T' 'E' 'C'       (IronTriangle Encrypted Compressed)
 *    Bytes 4-7:  original_size  (uint32_t little-endian)
 *    Bytes 8+:   RC4( stream de tokens LZ77 )
 *
 * El header (8 bytes) NUNCA se encripta: el modo -d necesita leer
 * original_size antes de poder asignar memoria para la descompresión.
 * El magic distingue los dos formatos sin ambigüedad.
 */
static const uint8_t MAGIC_LZ77[4] = { 'L', 'Z', '7', '7' };
static const uint8_t MAGIC_ENC[4]  = { 'I', 'T', 'E', 'C' };
#define HDR_SIZE 8

static void usage(const char *prog) {
    fprintf(stderr,
        "Uso:\n"
        "  %s -c <entrada> <salida>   Comprimir con LZ77\n"
        "  %s -e <entrada> <salida>   Comprimir LZ77 + Encriptar RC4\n"
        "  %s -d <entrada> <salida>   Descifrar y/o Descomprimir\n",
        prog, prog, prog);
    exit(1);
}

/* -------------------------------------------------------------------------
 * do_compress — LZ77 solo, sin encriptación.
 * -------------------------------------------------------------------------
 */
static int do_compress(const char *in_path, const char *out_path) {
    uint8_t *src = NULL;
    size_t   src_len = 0;
    if (read_file(in_path, &src, &src_len) < 0) return 1;

    size_t   cap = src_len * LZ77_TOKEN_BYTES + HDR_SIZE;
    uint8_t *buf = malloc(cap);
    if (!buf) { perror("malloc"); free(src); return 1; }

    ssize_t comp_len = lz77_compress(src, src_len, buf + HDR_SIZE, cap - HDR_SIZE);
    uint32_t orig32  = (uint32_t)src_len;
    free(src);
    if (comp_len < 0) { free(buf); return 1; }

    memcpy(buf, MAGIC_LZ77, 4);
    memcpy(buf + 4, &orig32, 4);

    fprintf(stderr, "[lz77]     %u → %zd bytes (%.1f%%)\n",
            orig32, comp_len, 100.0 * (double)comp_len / (double)orig32);

    int ret = write_file(out_path, buf, HDR_SIZE + (size_t)comp_len);
    free(buf);
    return ret < 0 ? 1 : 0;
}

/* -------------------------------------------------------------------------
 * do_encrypt — pipeline completo: LZ77 compress → RC4 encrypt → write.
 *
 * Todo ocurre en RAM. El orden es obligatorio por entropía:
 *   comprimir PRIMERO (los datos tienen patrones) →
 *   encriptar DESPUÉS  (los datos cifrados tienen alta entropía y no comprimen).
 *
 * Flujo de memoria:
 *   src_buf  (input original)
 *      │  lz77_compress()
 *      ▼
 *   out_buf+HDR_SIZE  (tokens LZ77 en RAM)
 *      │  rc4_crypt() in-place
 *      ▼
 *   out_buf+HDR_SIZE  (tokens cifrados en RAM)
 *      │  write_file() — UNA sola syscall write()
 *      ▼
 *   disco
 * -------------------------------------------------------------------------
 */
static int do_encrypt(const char *in_path, const char *out_path) {
    uint8_t *src = NULL;
    size_t   src_len = 0;
    if (read_file(in_path, &src, &src_len) < 0) return 1;

    /* Reservar: header + peor caso LZ77 (sin matches: 3 bytes por byte) */
    size_t   cap = src_len * LZ77_TOKEN_BYTES + HDR_SIZE;
    uint8_t *buf = malloc(cap);
    if (!buf) { perror("malloc"); free(src); return 1; }

    /* Stage 1: comprimir en RAM (dejar espacio para el header al inicio) */
    ssize_t comp_len = lz77_compress(src, src_len, buf + HDR_SIZE, cap - HDR_SIZE);
    uint32_t orig32  = (uint32_t)src_len;
    free(src);
    if (comp_len < 0) { free(buf); return 1; }

    fprintf(stderr, "[lz77]     %u → %zd bytes (%.1f%%)\n",
            orig32, comp_len, 100.0 * (double)comp_len / (double)orig32);

    /*
     * Stage 2: obtener clave e inicializar RC4.
     * get_key_secure() ejecuta: mlock → getpass → rc4_init → explicit_bzero → munlock
     * Al retornar, la clave ya fue borrada de RAM. El S-box de ctx está listo.
     */
    RC4_CTX ctx;
    if (get_key_secure(&ctx, "Contraseña de cifrado: ") < 0) {
        free(buf);
        return 1;
    }

    /* Stage 3: cifrar el stream comprimido in-place (XOR con keystream RC4) */
    rc4_crypt(&ctx, buf + HDR_SIZE, (size_t)comp_len);
    explicit_bzero(&ctx, sizeof(ctx));  /* borrar S-box del contexto */

    /* Escribir header "ITEC" + stream cifrado en una sola llamada write() */
    memcpy(buf, MAGIC_ENC, 4);
    memcpy(buf + 4, &orig32, 4);

    fprintf(stderr, "[rc4]      cifrado in-place (%zd bytes)\n", comp_len);
    fprintf(stderr, "[write]    → %s\n", out_path);

    int ret = write_file(out_path, buf, HDR_SIZE + (size_t)comp_len);
    free(buf);
    return ret < 0 ? 1 : 0;
}

/* -------------------------------------------------------------------------
 * do_decompress — inversa automática según el magic del archivo.
 *
 *  "LZ77": solo descomprimir (sin pedir clave)
 *  "ITEC": RC4 decrypt in-place → LZ77 decompress
 *
 * El header nunca fue cifrado, así que podemos leer el magic y
 * original_size antes de pedir la clave o asignar memoria.
 * -------------------------------------------------------------------------
 */
static int do_decompress(const char *in_path, const char *out_path) {
    uint8_t *src = NULL;
    size_t   src_len = 0;
    if (read_file(in_path, &src, &src_len) < 0) return 1;

    if (src_len < HDR_SIZE) {
        fprintf(stderr, "error: archivo demasiado pequeño\n");
        free(src);
        return 1;
    }

    uint32_t orig_size;
    memcpy(&orig_size, src + 4, 4);

    uint8_t *token_stream = src + HDR_SIZE;
    size_t   token_len    = src_len - HDR_SIZE;

    if (memcmp(src, MAGIC_ENC, 4) == 0) {
        /*
         * Formato ITEC: descifrar primero.
         * Misma función rc4_crypt(), mismo orden: XOR es su propio inverso.
         * El S-box se reinicializa desde cero con la misma clave → mismo keystream.
         */
        RC4_CTX ctx;
        if (get_key_secure(&ctx, "Contraseña de descifrado: ") < 0) {
            free(src);
            return 1;
        }
        rc4_crypt(&ctx, token_stream, token_len);
        explicit_bzero(&ctx, sizeof(ctx));

        fprintf(stderr, "[rc4]      descifrado in-place (%zu bytes)\n", token_len);

    } else if (memcmp(src, MAGIC_LZ77, 4) != 0) {
        fprintf(stderr, "error: formato desconocido (magic inválido)\n");
        free(src);
        return 1;
    }

    /* Descomprimir (común para ambos formatos) */
    uint8_t *out_buf = malloc(orig_size);
    if (!out_buf) { perror("malloc"); free(src); return 1; }

    ssize_t decomp_len = lz77_decompress(token_stream, token_len,
                                         out_buf, (size_t)orig_size);
    free(src);
    if (decomp_len < 0) { free(out_buf); return 1; }

    fprintf(stderr, "[lz77]     descomprimido → %zd bytes\n", decomp_len);
    fprintf(stderr, "[write]    → %s\n", out_path);

    int ret = write_file(out_path, out_buf, (size_t)decomp_len);
    free(out_buf);
    return ret < 0 ? 1 : 0;
}

int main(int argc, char *argv[]) {
    if (argc != 4) usage(argv[0]);

    const char *mode     = argv[1];
    const char *in_path  = argv[2];
    const char *out_path = argv[3];

    if (strcmp(mode, "-c") == 0) return do_compress(in_path, out_path);
    if (strcmp(mode, "-e") == 0) return do_encrypt(in_path, out_path);
    if (strcmp(mode, "-d") == 0) return do_decompress(in_path, out_path);

    usage(argv[0]);
    return 1;
}
