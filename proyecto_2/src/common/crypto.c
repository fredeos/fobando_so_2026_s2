#include "common/crypto.h"

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

void crypto_clear(void *buffer, size_t size)
{
    if (buffer != NULL && size != 0) {
        OPENSSL_cleanse(buffer, size);
    }
}

static int hex_value(unsigned char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

int crypto_load_key(uint8_t key[CRYPTO_KEY_SIZE])
{
    const char *value;

    if (key == NULL) {
        errno = EINVAL;
        return -1;
    }
    crypto_clear(key, CRYPTO_KEY_SIZE);
    value = getenv("DIPS_AES_KEY");
    if (value == NULL || strlen(value) != CRYPTO_KEY_SIZE * 2) {
        errno = EINVAL;
        return -1;
    }
    for (size_t i = 0; i < CRYPTO_KEY_SIZE; ++i) {
        int high = hex_value((unsigned char)value[2 * i]);
        int low = hex_value((unsigned char)value[2 * i + 1]);
        if (high < 0 || low < 0) {
            crypto_clear(key, CRYPTO_KEY_SIZE);
            errno = EINVAL;
            return -1;
        }
        key[i] = (uint8_t)((high << 4) | low);
    }
    return 0;
}

static int valid_buffers(const uint8_t *key, const uint8_t *input,
                         size_t size, const uint8_t *aad, size_t aad_size,
                         const uint8_t *nonce, const uint8_t *tag)
{
    if (key == NULL || nonce == NULL || tag == NULL ||
        (size != 0 && input == NULL) || (aad_size != 0 && aad == NULL)) {
        errno = EINVAL;
        return 0;
    }
    if (size > INT_MAX || aad_size > INT_MAX) {
        errno = EMSGSIZE;
        return 0;
    }
    return 1;
}

int crypto_encrypt(const uint8_t key[CRYPTO_KEY_SIZE],
                   const uint8_t *plaintext, size_t plaintext_size,
                   const uint8_t *aad, size_t aad_size,
                   uint8_t nonce[CRYPTO_NONCE_SIZE],
                   uint8_t **ciphertext, uint8_t tag[CRYPTO_TAG_SIZE])
{
    EVP_CIPHER_CTX *ctx = NULL;
    uint8_t *output = NULL;
    int written = 0, final_size = 0;
    int error = EIO;

    if (ciphertext == NULL) {
        errno = EINVAL;
        return -1;
    }
    *ciphertext = NULL;
    if (!valid_buffers(key, plaintext, plaintext_size, aad, aad_size,
                       nonce, tag)) return -1;

    /* Un byte adicional permite Final incluso para un mensaje vacío. */
    output = malloc(plaintext_size + 1);
    ctx = EVP_CIPHER_CTX_new();
    if (output == NULL || ctx == NULL) {
        error = ENOMEM;
        goto fail;
    }
    if (RAND_bytes(nonce, CRYPTO_NONCE_SIZE) != 1 ||
        EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL) != 1 ||
        EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN,
                            CRYPTO_NONCE_SIZE, NULL) != 1 ||
        EVP_EncryptInit_ex(ctx, NULL, NULL, key, nonce) != 1) goto fail;
    if (aad_size != 0 &&
        EVP_EncryptUpdate(ctx, NULL, &written, aad, (int)aad_size) != 1)
        goto fail;
    written = 0;
    if (plaintext_size != 0 &&
        EVP_EncryptUpdate(ctx, output, &written, plaintext,
                          (int)plaintext_size) != 1) goto fail;
    if ((size_t)written != plaintext_size ||
        EVP_EncryptFinal_ex(ctx, output + written, &final_size) != 1 ||
        final_size != 0 ||
        EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG,
                            CRYPTO_TAG_SIZE, tag) != 1) goto fail;
    EVP_CIPHER_CTX_free(ctx);
    *ciphertext = output;
    return 0;

fail:
    EVP_CIPHER_CTX_free(ctx);
    crypto_clear(output, plaintext_size + 1);
    free(output);
    errno = error;
    return -1;
}

int crypto_decrypt(const uint8_t key[CRYPTO_KEY_SIZE],
                   const uint8_t *ciphertext, size_t ciphertext_size,
                   const uint8_t *aad, size_t aad_size,
                   const uint8_t nonce[CRYPTO_NONCE_SIZE],
                   const uint8_t tag[CRYPTO_TAG_SIZE], uint8_t **plaintext)
{
    EVP_CIPHER_CTX *ctx = NULL;
    uint8_t *output = NULL;
    uint8_t tag_copy[CRYPTO_TAG_SIZE];
    int written = 0, final_size = 0;
    int error = EIO;

    if (plaintext == NULL) {
        errno = EINVAL;
        return -1;
    }
    *plaintext = NULL;
    if (!valid_buffers(key, ciphertext, ciphertext_size, aad, aad_size,
                       nonce, tag)) return -1;
    memcpy(tag_copy, tag, sizeof(tag_copy));
    output = malloc(ciphertext_size + 1);
    ctx = EVP_CIPHER_CTX_new();
    if (output == NULL || ctx == NULL) {
        error = ENOMEM;
        goto fail;
    }
    if (EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL) != 1 ||
        EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN,
                            CRYPTO_NONCE_SIZE, NULL) != 1 ||
        EVP_DecryptInit_ex(ctx, NULL, NULL, key, nonce) != 1) goto fail;
    if (aad_size != 0 &&
        EVP_DecryptUpdate(ctx, NULL, &written, aad, (int)aad_size) != 1)
        goto fail;
    written = 0;
    if (ciphertext_size != 0 &&
        EVP_DecryptUpdate(ctx, output, &written, ciphertext,
                          (int)ciphertext_size) != 1) goto fail;
    if ((size_t)written != ciphertext_size ||
        EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG,
                            CRYPTO_TAG_SIZE, tag_copy) != 1) goto fail;
    if (EVP_DecryptFinal_ex(ctx, output + written, &final_size) != 1) {
        error = EBADMSG;
        goto fail;
    }
    if (final_size != 0) goto fail;
    EVP_CIPHER_CTX_free(ctx);
    crypto_clear(tag_copy, sizeof(tag_copy));
    *plaintext = output;
    return 0;

fail:
    EVP_CIPHER_CTX_free(ctx);
    crypto_clear(tag_copy, sizeof(tag_copy));
    crypto_clear(output, ciphertext_size + 1);
    free(output);
    errno = error;
    return -1;
}
