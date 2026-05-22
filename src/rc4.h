#ifndef RC4_H
#define RC4_H

#include <stddef.h>
#include <stdint.h>

/*
 * RC4_CTX — estado completo del cifrador RC4.
 *
 * El S-box (256 bytes) es una permutación de 0..255 inicializada por el KSA
 * con la clave, y continuamente modificada por el PRGA durante el cifrado.
 * Los índices i y j controlan la posición en esa permutación.
 *
 * Guardar el contexto en un struct (no en variables estáticas) hace la
 * implementación reentrante: podemos tener múltiples streams RC4 simultáneos
 * y procesar archivos en trozos sin perder el estado del keystream.
 */
typedef struct {
    uint8_t S[256]; /* S-box: permutación de 0..255 generada por la clave */
    uint8_t i;      /* índice PRGA i — avanza 1 en cada byte cifrado        */
    uint8_t j;      /* índice PRGA j — depende de S[i] y avanza variable    */
} RC4_CTX;

/*
 * rc4_init — KSA: inicializa el S-box a partir de la clave.
 * keylen debe estar en [1, 256]. Claves más largas no mejoran la seguridad
 * de RC4 (el S-box solo tiene 256 entradas).
 */
void rc4_init(RC4_CTX *ctx, const uint8_t *key, size_t keylen);

/*
 * rc4_crypt — PRGA: cifra/descifra data in-place con el keystream RC4.
 *
 * XOR es su propia inversa: (P ^ K) ^ K == P.
 * Por eso la misma función sirve para cifrar y para descifrar: solo hay
 * que llamarla con el mismo contexto (misma clave, misma posición inicial).
 *
 * Soporta modo streaming: llamadas sucesivas a rc4_crypt() sobre el mismo
 * ctx continúan el keystream donde lo dejaron.
 */
void rc4_crypt(RC4_CTX *ctx, uint8_t *data, size_t len);

/*
 * rc4_selftest — cifra "hello world", descifra, verifica igualdad.
 * Returns 0 si pasa, -1 si falla.
 */
int rc4_selftest(void);

#endif /* RC4_H */
