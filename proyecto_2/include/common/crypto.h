#ifndef DIPS_CRYPTO_H
#define DIPS_CRYPTO_H

#include <stddef.h>
#include <stdint.h>

#define CRYPTO_KEY_SIZE 32u // 32 bytes equivalen a la clave de AES-256.
#define CRYPTO_NONCE_SIZE 12u // Nonce aleatorio de 96 bits para cada mensaje GCM.
#define CRYPTO_TAG_SIZE 16u // Etiqueta de 128 bits para detectar alteraciones.

/* Las funciones int devuelven 0 al tener éxito y -1 con errno al fallar. */

/* Lee DIPS_AES_KEY: exactamente 64 dígitos hexadecimales, sin prefijos. */
int crypto_load_key(uint8_t key[CRYPTO_KEY_SIZE]);

/* AES-256-GCM. Genera un nonce con RAND_bytes en cada llamada.
 * ciphertext recibe memoria propia que el llamador debe liberar con free().
 * Su longitud es plaintext_size; aad se autentica, pero no se cifra.
 * En errores, *ciphertext queda en NULL. Se admiten buffers vacíos. */
int crypto_encrypt(const uint8_t key[CRYPTO_KEY_SIZE],
                   const uint8_t *plaintext, size_t plaintext_size,
                   const uint8_t *aad, size_t aad_size,
                   uint8_t nonce[CRYPTO_NONCE_SIZE],
                   uint8_t **ciphertext, uint8_t tag[CRYPTO_TAG_SIZE]);

/* Solo entrega plaintext después de verificar la etiqueta; EBADMSG indica
 * autenticación fallida. Su longitud es ciphertext_size. El llamador debe
 * borrar y liberar el resultado; ante cualquier error queda en NULL. */
int crypto_decrypt(const uint8_t key[CRYPTO_KEY_SIZE],
                   const uint8_t *ciphertext, size_t ciphertext_size,
                   const uint8_t *aad, size_t aad_size,
                   const uint8_t nonce[CRYPTO_NONCE_SIZE],
                   const uint8_t tag[CRYPTO_TAG_SIZE], uint8_t **plaintext);

/* Borra datos sensibles sin que el compilador elimine la operación. */
void crypto_clear(void *buffer, size_t size);

#endif
