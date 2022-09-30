/* copyright (c) 2022 grunfink - MIT license */

#ifndef _XS_ENCDEC_H

#define _XS_ENCDEC_H

d_char *xs_hex_enc(const char *data, int size);
d_char *xs_hex_dec(const char *hex);
d_char *xs_base64_enc(const char *data, int sz);
d_char *xs_base64_dec(const char *data, int *size);
d_char *xs_utf8_enc(d_char *str, unsigned int cpoint);


#ifdef XS_IMPLEMENTATION

d_char *xs_hex_enc(const char *data, int size)
/* returns an hexdump of data */
{
    d_char *s;
    char *p;
    int n;

    p = s = calloc(size * 2 + 1, 1);

    for (n = 0; n < size; n++) {
        sprintf(p, "%02x", (unsigned char)data[n]);
        p += 2;
    }

    return s;
}


d_char *xs_hex_dec(const char *hex)
/* decodes an hexdump into data */
{
    int sz = strlen(hex);
    d_char *s = NULL;
    char *p;
    int n;

    if (sz % 2)
        return s;

    p = s = calloc(sz / 2, 1);

    for (n = 0; n < sz; n += 2) {
        int i;
        if (sscanf(&hex[n], "%02x", &i) == 0) {
            /* decoding error */
            free(s);
            s = NULL;
        }
        else
            *p = i;

        p++;
    }

    return s;
}


d_char *xs_base64_enc(const char *data, int sz)
/* encodes data to base64 */
{
    d_char *s;
    unsigned char *p;
    char *i;
    int bsz, n;
    static char *b64_tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                           "abcdefghijklmnopqrstuvwxyz"
                           "0123456789+/";

    bsz = ((sz + 3 - 1) / 3) * 4;
    i = s = calloc(bsz + 1, 1);
    p = (unsigned char *)data;

    for (n = 0; n < sz; n += 3) {
        int l = sz - n;

        if (l == 1) {
            *i++ = b64_tbl[(p[n] >> 2) & 0x3f];
            *i++ = b64_tbl[(p[n] << 4) & 0x3f];
            *i++ = '=';
            *i++ = '=';
        }
        else
        if (l == 2) {
            *i++ = b64_tbl[(p[n] >> 2) & 0x3f];
            *i++ = b64_tbl[(p[n] << 4 | p[n + 1] >> 4) & 0x3f];
            *i++ = b64_tbl[(p[n + 1] << 2) & 0x3f];
            *i++ = '=';
        }
        else {
            *i++ = b64_tbl[(p[n] >> 2) & 0x3f];
            *i++ = b64_tbl[(p[n] << 4 | p[n + 1] >> 4) & 0x3f];
            *i++ = b64_tbl[(p[n + 1] << 2 | p[n + 2] >> 6) & 0x3f];
            *i++ = b64_tbl[(p[n + 2]) & 0x3f];
        }
    }

    return s;
}


d_char *xs_base64_dec(const char *data, int *size)
/* decodes data from base64 */
{
    d_char *s = NULL;
    int sz = 0;
    char *p;
    static char *b64_tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                           "abcdefghijklmnopqrstuvwxyz"
                           "0123456789+/=";

    p = (char *)data;

    /* size of data must be a multiple of 4 */
    if (strlen(p) % 4)
        return s;

    for (p = (char *)data; *p; p += 4) {
        int cs[4];
        int n;
        unsigned char tmp[3];

        for (n = 0; n < 4; n++) {
            char *ss = strchr(b64_tbl, p[n]);

            if (ss == NULL) {
                /* not a base64 char */
                free(s);
                return NULL;
            }

            cs[n] = ss - b64_tbl;
        }

        n = 0;

        /* first byte */
        tmp[n++] = cs[0] << 2 | ((cs[1] >> 4) & 0x0f);

        /* second byte */
        if (cs[2] != 64)
            tmp[n++] = cs[1] << 4 | ((cs[2] >> 2) & 0x3f);

        /* third byte */
        if (cs[3] != 64)
            tmp[n++] = cs[2] << 6 | (cs[3] & 0x3f);

        /* must be done manually because data can be pure binary */
        s = realloc(s, sz + n);
        memcpy(s + sz, tmp, n);
        sz += n;
    }

    *size = sz;

    return s;
}


d_char *xs_utf8_enc(d_char *str, unsigned int cpoint)
/* encodes an Unicode codepoint to utf8 */
{
    unsigned char tmp[4];
    int n = 0;

    if (cpoint < 0x80)
        tmp[n++] = cpoint & 0xff;
    else
    if (cpoint < 0x800) {
        tmp[n++] = 0xc0 | (cpoint >> 6);
        tmp[n++] = 0x80 | (cpoint & 0x3f);
    }
    else
    if (cpoint < 0x10000) {
        tmp[n++] = 0xe0 | (cpoint >> 12);
        tmp[n++] = 0x80 | ((cpoint >> 6) & 0x3f);
        tmp[n++] = 0x80 | (cpoint & 0x3f);
    }
    else
    if (cpoint < 0x200000) {
        tmp[n++] = 0xf0 | (cpoint >> 18);
        tmp[n++] = 0x80 | ((cpoint >> 12) & 0x3f);
        tmp[n++] = 0x80 | ((cpoint >> 6) & 0x3f);
        tmp[n++] = 0x80 | (cpoint & 0x3f);
    }

    return xs_append_m(str, (char *)tmp, n);
}

#endif /* XS_IMPLEMENTATION */

#endif /* _XS_ENCDEC_H */
