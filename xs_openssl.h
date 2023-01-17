/* copyright (c) 2022 - 2023 grunfink / MIT license */

#ifndef _XS_OPENSSL_H

#define _XS_OPENSSL_H

d_char *xs_md5_hex(const void *input, int size);
d_char *xs_sha1_hex(const void *input, int size);
d_char *xs_sha256_hex(const void *input, int size);
d_char *xs_sha256_base64(const void *input, int size);
d_char *xs_rsa_genkey(int bits);
d_char *xs_rsa_sign(const char *secret, const char *mem, int size);
int xs_rsa_verify(const char *pubkey, const char *mem, int size, const char *b64sig);
d_char *xs_evp_sign(const char *secret, const char *mem, int size);
int xs_evp_verify(const char *pubkey, const char *mem, int size, const char *b64sig);


#ifdef XS_IMPLEMENTATION

#include "openssl/md5.h"
#include "openssl/sha.h"
#include "openssl/rsa.h"
#include "openssl/pem.h"
#include "openssl/evp.h"

d_char *xs_md5_hex(const void *input, int size)
{
    unsigned char md5[16];
    MD5_CTX ctx;

    MD5_Init(&ctx);
    MD5_Update(&ctx, input, size);
    MD5_Final(md5, &ctx);

    return xs_hex_enc((char *)md5, sizeof(md5));
}


d_char *xs_sha1_hex(const void *input, int size)
{
    unsigned char sha1[20];
    SHA_CTX ctx;

    SHA1_Init(&ctx);
    SHA1_Update(&ctx, input, size);
    SHA1_Final(sha1, &ctx);

    return xs_hex_enc((char *)sha1, sizeof(sha1));
}


unsigned char *_xs_sha256(const void *input, int size, unsigned char *sha256)
{
    SHA256_CTX ctx;

    SHA256_Init(&ctx);
    SHA256_Update(&ctx, input, size);
    SHA256_Final(sha256, &ctx);

    return sha256;
}


d_char *xs_sha256_hex(const void *input, int size)
{
    unsigned char sha256[32];

    _xs_sha256(input, size, sha256);

    return xs_hex_enc((char *)sha256, sizeof(sha256));
}


d_char *xs_sha256_base64(const void *input, int size)
{
    unsigned char sha256[32];

    _xs_sha256(input, size, sha256);

    return xs_base64_enc((char *)sha256, sizeof(sha256));
}


d_char *xs_rsa_genkey(int bits)
/* generates an RSA keypair */
{
    BIGNUM *bne;
    RSA *rsa;
    d_char *keypair = NULL;

    if ((bne = BN_new()) != NULL) {
        if (BN_set_word(bne, RSA_F4) == 1) {
            if ((rsa = RSA_new()) != NULL) {
                if (RSA_generate_key_ex(rsa, bits, bne, NULL) == 1) {
                    BIO *bs = BIO_new(BIO_s_mem());
                    BIO *bp = BIO_new(BIO_s_mem());
                    BUF_MEM *sptr;
                    BUF_MEM *pptr;

                    PEM_write_bio_RSAPrivateKey(bs, rsa, NULL, NULL, 0, 0, NULL);
                    BIO_get_mem_ptr(bs, &sptr);

                    PEM_write_bio_RSA_PUBKEY(bp, rsa);
                    BIO_get_mem_ptr(bp, &pptr);

                    keypair = xs_dict_new();

                    keypair = xs_dict_append(keypair, "secret", sptr->data);
                    keypair = xs_dict_append(keypair, "public", pptr->data);

                    BIO_free(bs);
                    BIO_free(bp);
                }
            }
        }
    }

    return keypair;    
}


d_char *xs_rsa_sign(const char *secret, const char *mem, int size)
/* signs a memory block (secret is in PEM format) */
{
    d_char *signature = NULL;
    BIO *b;
    RSA *rsa;
    unsigned char *sig;
    unsigned int sig_len;

    /* un-PEM the key */
    b = BIO_new_mem_buf(secret, strlen(secret));
    rsa = PEM_read_bio_RSAPrivateKey(b, NULL, NULL, NULL);

    /* alloc space */
    sig = xs_realloc(NULL, RSA_size(rsa));

    if (RSA_sign(NID_sha256, (unsigned char *)mem, size, sig, &sig_len, rsa) == 1)
        signature = xs_base64_enc((char *)sig, sig_len);

    BIO_free(b);
    RSA_free(rsa);
    xs_free(sig);

    return signature;
}


int xs_rsa_verify(const char *pubkey, const char *mem, int size, const char *b64sig)
/* verifies a base64 block, returns non-zero on ok */
{
    int r = 0;
    BIO *b;
    RSA *rsa;

    /* un-PEM the key */
    b = BIO_new_mem_buf(pubkey, strlen(pubkey));
    rsa = PEM_read_bio_RSA_PUBKEY(b, NULL, NULL, NULL);

    if (rsa != NULL) {
        xs *sig = NULL;
        int s_size;

        /* de-base64 */
        sig = xs_base64_dec(b64sig,  &s_size);

        if (sig != NULL)
            r = RSA_verify(NID_sha256, (unsigned char *)mem, size,
                           (unsigned char *)sig, s_size, rsa);
    }

    BIO_free(b);
    RSA_free(rsa);

    return r;
}


d_char *xs_evp_sign(const char *secret, const char *mem, int size)
/* signs a memory block (secret is in PEM format) */
{
    d_char *signature = NULL;
    BIO *b;
    unsigned char *sig;
    unsigned int sig_len;
    EVP_PKEY *pkey;
    EVP_MD_CTX *mdctx;
    const EVP_MD *md;

    /* un-PEM the key */
    b = BIO_new_mem_buf(secret, strlen(secret));
    pkey = PEM_read_bio_PrivateKey(b, NULL, NULL, NULL);

    /* I've learnt all these magical incantations by watching
       the Python module code and the OpenSSL manual pages */
    /* Well, "learnt" may be an overstatement */

    md = EVP_get_digestbyname("sha256");

    mdctx = EVP_MD_CTX_new();

    sig_len = EVP_PKEY_size(pkey);
    sig = xs_realloc(NULL, sig_len);

    EVP_SignInit(mdctx, md);
    EVP_SignUpdate(mdctx, mem, size);

    if (EVP_SignFinal(mdctx, sig, &sig_len, pkey) == 1)
        signature = xs_base64_enc((char *)sig, sig_len);

    EVP_MD_CTX_free(mdctx);
    EVP_PKEY_free(pkey);
    BIO_free(b);
    xs_free(sig);

    return signature;
}


int xs_evp_verify(const char *pubkey, const char *mem, int size, const char *b64sig)
/* verifies a base64 block, returns non-zero on ok */
{
    int r = 0;
    BIO *b;
    EVP_PKEY *pkey;
    EVP_MD_CTX *mdctx;
    const EVP_MD *md;

    /* un-PEM the key */
    b = BIO_new_mem_buf(pubkey, strlen(pubkey));
    pkey = PEM_read_bio_PUBKEY(b, NULL, NULL, NULL);

    md = EVP_get_digestbyname("sha256");
    mdctx = EVP_MD_CTX_new();

    if (pkey != NULL) {
        xs *sig = NULL;
        int s_size;

        /* de-base64 */
        sig = xs_base64_dec(b64sig,  &s_size);

        if (sig != NULL) {
            EVP_VerifyInit(mdctx, md);
            EVP_VerifyUpdate(mdctx, mem, size);

            r = EVP_VerifyFinal(mdctx, (unsigned char *)sig, s_size, pkey);
        }
    }

    EVP_MD_CTX_free(mdctx);
    EVP_PKEY_free(pkey);
    BIO_free(b);

    return r;
}


#endif /* XS_IMPLEMENTATION */

#endif /* _XS_OPENSSL_H */
