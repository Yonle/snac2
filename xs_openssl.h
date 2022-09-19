/* copyright (c) 2022 grunfink - MIT license */

#ifndef _XS_OPENSSL_H

#define _XS_OPENSSL_H

d_char *xs_md5_hex(const void *input, int size);
d_char *xs_sha1_hex(const void *input, int size);
d_char *xs_sha256_hex(const void *input, int size);
d_char *xs_rsa_genkey(int bits);
d_char *xs_rsa_sign(char *secret, char *mem, int size);
int xs_rsa_verify(char *pubkey, char *mem, int size, char *b64sig);


#ifdef XS_IMPLEMENTATION

#include "openssl/md5.h"
#include "openssl/sha.h"
#include "openssl/rsa.h"
#include "openssl/pem.h"

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


d_char *xs_sha256_hex(const void *input, int size)
{
    unsigned char sha256[32];
    SHA256_CTX ctx;

    SHA256_Init(&ctx);
    SHA256_Update(&ctx, input, size);
    SHA256_Final(sha256, &ctx);

    return xs_hex_enc((char *)sha256, sizeof(sha256));
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


d_char *xs_rsa_sign(char *secret, char *mem, int size)
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
    sig = malloc(RSA_size(rsa));

    if (RSA_sign(NID_sha256, (unsigned char *)mem, size, sig, &sig_len, rsa) == 1)
        signature = xs_base64_enc((char *)sig, sig_len);

    BIO_free(b);
    RSA_free(rsa);
    free(sig);

    return signature;
}


int xs_rsa_verify(char *pubkey, char *mem, int size, char *b64sig)
/* verifies a base64 block, returns non-zero on ok */
{
    int r = 0;
    BIO *b;
    RSA *rsa;

    /* un-PEM the key */
    b = BIO_new_mem_buf(pubkey, strlen(pubkey));
    rsa = PEM_read_bio_RSA_PUBKEY(b, NULL, NULL, NULL);

    if (rsa != NULL) {
        d_char *sig = NULL;
        int s_size;

        /* de-base64 */
        sig = xs_base64_dec(b64sig,  &s_size);

        if (sig != NULL)
            r = RSA_verify(NID_sha256, (unsigned char *)mem, size,
                           (unsigned char *)sig, s_size, rsa);

        free(sig);
    }

    BIO_free(b);
    RSA_free(rsa);

    return r;
}

#endif /* XS_IMPLEMENTATION */

#endif /* _XS_OPENSSL_H */
