#include "rc4.h"

#include <string.h>
#include <stdio.h>

/* =========================================================================
 * KSA — Key Scheduling Algorithm
 *
 * OBJETIVO: transformar una clave de longitud arbitraria (1–256 bytes) en
 * una permutación de 0..255 almacenada en S. Esa permutación es la "huella"
 * de la clave sobre el S-box.
 *
 * PASOS:
 *   1. Inicializar S como la permutación identidad: S[i] = i
 *   2. j = 0
 *   3. Para i = 0..255:
 *        j = (j + S[i] + key[i % keylen]) mod 256
 *        swap(S[i], S[j])
 *
 * INTUICIÓN:
 *   Cada iteración mezcla la posición actual S[i] con:
 *     - el acumulador j (memoria de las rondas anteriores)
 *     - key[i % keylen] (el byte de clave para esta posición)
 *   El módulo 256 se da naturalmente por el overflow de uint8_t.
 *   Al finalizar, todas las 256 entradas de S han sido movidas al menos
 *   una vez, y el patrón de movimientos depende completamente de la clave.
 * =========================================================================
 */
void rc4_init(RC4_CTX *ctx, const uint8_t *key, size_t keylen) {
    uint8_t *S = ctx->S;

    /* Paso 1: permutación identidad */
    for (int i = 0; i < 256; i++)
        S[i] = (uint8_t)i;

    /* Paso 2-3: mezclar la clave en el S-box */
    uint8_t j = 0;
    for (int i = 0; i < 256; i++) {
        /*
         * j avanza de forma pseudoaleatoria: depende del contenido de S[i]
         * (que ya fue modificado en rondas anteriores) y del byte de clave.
         * El cast a uint8_t hace el módulo 256 de forma implícita y eficiente.
         */
        j = (uint8_t)(j + S[i] + key[i % keylen]);

        /* swap S[i] ↔ S[j] */
        uint8_t tmp = S[i];
        S[i]        = S[j];
        S[j]        = tmp;
    }

    /* Resetear índices PRGA al estado inicial */
    ctx->i = 0;
    ctx->j = 0;
}

/* =========================================================================
 * PRGA — Pseudo-Random Generation Algorithm
 *
 * OBJETIVO: generar un keystream de bytes pseudoaleatorios usando el S-box
 * inicializado por el KSA. Cada byte del keystream se XOR con el dato.
 *
 * POR CADA BYTE DE DATOS:
 *   i = (i + 1)    mod 256        ← avanza i siempre en 1
 *   j = (j + S[i]) mod 256        ← avanza j de forma variable según S
 *   swap(S[i], S[j])              ← modifica el S-box continuamente
 *   keystream = S[(S[i] + S[j]) mod 256]
 *   output    = data XOR keystream
 *
 * PROPIEDADES CLAVE:
 *   • XOR es su propia inversa: (P ^ K) ^ K == P
 *     → cifrar = descifrar con el mismo keystream
 *   • El S-box cambia con cada byte → keystream no se repite fácilmente
 *   • Soporta streaming: el estado (i, j, S) persiste entre llamadas a
 *     rc4_crypt(), permitiendo procesar el archivo en trozos de PAGE_SIZE
 *
 * NOTA DE SEGURIDAD:
 *   RC4 tiene vulnerabilidades conocidas (ataque FMS, RC4-biases en TLS,
 *   WEP-crack). Para este proyecto es adecuado como ejercicio educativo.
 *   En producción se usaría ChaCha20 o AES-GCM.
 * =========================================================================
 */
void rc4_crypt(RC4_CTX *ctx, uint8_t *data, size_t len) {
    uint8_t *S = ctx->S;
    uint8_t  i = ctx->i;
    uint8_t  j = ctx->j;

    for (size_t k = 0; k < len; k++) {
        /* Avanzar i — siempre +1, el overflow de uint8_t hace el mod 256 */
        i = (uint8_t)(i + 1);

        /* Avanzar j — variable, mezcla el valor actual de S[i] */
        j = (uint8_t)(j + S[i]);

        /* Mezclar S[i] ↔ S[j] — mantiene S como una permutación */
        uint8_t tmp = S[i];
        S[i]        = S[j];
        S[j]        = tmp;

        /*
         * Generar byte de keystream: índice = (S[i] + S[j]) mod 256
         * Combina los dos índices activos → mayor dispersión del keystream.
         * El cast a uint8_t hace el mod 256.
         */
        uint8_t keystream = S[(uint8_t)(S[i] + S[j])];

        /* XOR en-lugar: misma operación para cifrar y descifrar */
        data[k] ^= keystream;
    }

    /* Guardar estado para la próxima llamada (modo streaming) */
    ctx->i = i;
    ctx->j = j;
}

/* =========================================================================
 * rc4_selftest — prueba de sanidad: cifrar→descifrar → resultado == original
 *
 * Compilar y ejecutar standalone:
 *   gcc -DRC4_TEST -Wall -O2 -std=c99 -o rc4_test src/rc4.c && ./rc4_test
 * =========================================================================
 */
int rc4_selftest(void) {
    static const char    plaintext[] = "hello world";
    static const uint8_t key[]       = "secretkey";
    const size_t         len         = sizeof(plaintext) - 1;  /* sin '\0' */
    const size_t         keylen      = sizeof(key) - 1;

    uint8_t buf[sizeof(plaintext)];
    memcpy(buf, plaintext, len);

    RC4_CTX ctx;

    /* --- Cifrar --- */
    rc4_init(&ctx, key, keylen);
    rc4_crypt(&ctx, buf, len);

    /* El texto cifrado NO debe ser igual al plaintext */
    if (memcmp(buf, plaintext, len) == 0) {
        fprintf(stderr, "[rc4_selftest] FAIL: ciphertext == plaintext\n");
        return -1;
    }

    /* Mostrar bytes cifrados (hex) para verificación visual */
    fprintf(stderr, "[rc4_selftest] ciphertext: ");
    for (size_t i = 0; i < len; i++)
        fprintf(stderr, "%02x ", buf[i]);
    fprintf(stderr, "\n");

    /* --- Descifrar: misma clave, misma función --- */
    rc4_init(&ctx, key, keylen);
    rc4_crypt(&ctx, buf, len);

    /* El resultado debe ser idéntico al texto original */
    if (memcmp(buf, plaintext, len) != 0) {
        fprintf(stderr, "[rc4_selftest] FAIL: decrypt != original\n");
        return -1;
    }

    fprintf(stderr, "[rc4_selftest] PASS: \"%s\" → cifrado → \"%.*s\"\n",
            plaintext, (int)len, (char *)buf);
    return 0;
}

/* =========================================================================
 * main() de prueba standalone — solo se compila con -DRC4_TEST
 * =========================================================================
 */
#ifdef RC4_TEST
int main(void) {
    return rc4_selftest() == 0 ? 0 : 1;
}
#endif
