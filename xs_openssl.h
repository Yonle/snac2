/* copyright (c) 2022 - 2023 grunfink / MIT license */

#ifndef _XS_OPENSSL_H

#define _XS_OPENSSL_H

xs_str *_xs_digest(const xs_val *input, int size, const char *digest, int as_hex);

#define xs_md5_hex(input, size)       _xs_digest(input, size, "md5",    1)
#define xs_sha1_hex(input, size)      _xs_digest(input, size, "sha1",   1)
#define xs_sha256_hex(input, size)    _xs_digest(input, size, "sha256", 1)
#define xs_sha256_base64(input, size) _xs_digest(input, size, "sha256", 0)

xs_dict *xs_rsa_genkey(int bits);
xs_str *xs_rsa_sign(const char *secret, const char *mem, int size);
int xs_rsa_verify(const char *pubkey, const char *mem, int size, const char *b64sig);
xs_str *xs_evp_sign(const char *secret, const char *mem, int size);
int xs_evp_verify(const char *pubkey, const char *mem, int size, const char *b64sig);


#ifdef XS_IMPLEMENTATION

#include "openssl/rsa.h"
#include "openssl/pem.h"
#include "openssl/evp.h"

xs_str *_xs_digest(const xs_val *input, int size, const char *digest, int as_hex)
/* generic function for generating and encoding digests */
{
    const EVP_MD *md;

    if ((md = EVP_get_digestbyname(digest)) == NULL)
        return NULL;

    unsigned char output[1024];
    unsigned int out_size;
    EVP_MD_CTX *mdctx;

    mdctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(mdctx, md, NULL);
    EVP_DigestUpdate(mdctx, input, size);
    EVP_DigestFinal_ex(mdctx, output, &out_size);
    EVP_MD_CTX_free(mdctx);

    return as_hex ? xs_hex_enc   ((char *)output, out_size) :
                    xs_base64_enc((char *)output, out_size);
}


xs_dict *xs_rsa_genkey(int bits)
/* generates an RSA keypair */
{
    BIGNUM *bne;
    RSA *rsa;
    xs_dict *keypair = NULL;

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


xs_str *xs_rsa_sign(const char *secret, const char *mem, int size)
/* signs a memory block (secret is in PEM format) */
{
    xs_str *signature = NULL;
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


xs_str *xs_evp_sign(const char *secret, const char *mem, int size)
/* signs a memory block (secret is in PEM format) */
{
    xs_str *signature = NULL;
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
