/* copyright (c) 2022 - 2023 grunfink / MIT license */

#ifndef _XS_UNICODE_H

#define _XS_UNICODE_H

 xs_str *xs_utf8_enc(xs_str *str, unsigned int cpoint);
 char *xs_utf8_dec(const char *str, unsigned int *cpoint);


#ifdef XS_IMPLEMENTATION


char *_xs_utf8_enc(char buf[4], unsigned int cpoint)
/* encodes an Unicode codepoint to utf-8 into buf and returns the new position */
{
    unsigned char *p = (unsigned char *)buf;

    if (cpoint < 0x80) /* 1 byte char */
        *p++ = cpoint & 0xff;
    else {
        if (cpoint < 0x800) /* 2 byte char */
            *p++ = 0xc0 | (cpoint >> 6);
        else {
            if (cpoint < 0x10000) /* 3 byte char */
                *p++ = 0xe0 | (cpoint >> 12);
            else { /* 4 byte char */
                *p++ = 0xf0 | (cpoint >> 18);
                *p++ = 0x80 | ((cpoint >> 12) & 0x3f);
            }

            *p++ = 0x80 | ((cpoint >> 6) & 0x3f);
        }

        *p++ = 0x80 | (cpoint & 0x3f);
    }

    return (char *)p;
}


xs_str *xs_utf8_enc(xs_str *str, unsigned int cpoint)
/* encodes an Unicode codepoint to utf-8 into str */
{
    char tmp[4], *p;

    p = _xs_utf8_enc(tmp, cpoint);

    return xs_append_m(str, tmp, p - tmp);
}


char *xs_utf8_dec(const char *str, unsigned int *cpoint)
/* decodes an utf-8 char inside str into cpoint and returns the next position */
{
    unsigned char *p = (unsigned char *)str;
    int c = *p++;
    int cb = 0;

    if ((c & 0x80) == 0) { /* 1 byte char */
        *cpoint = c;
    }
    else
    if ((c & 0xe0) == 0xc0) { /* 2 byte char */
        *cpoint = (c & 0x1f) << 6;
        cb = 1;
    }
    else
    if ((c & 0xf0) == 0xe0) { /* 3 byte char */
        *cpoint = (c & 0x0f) << 12;
        cb = 2;
    }
    else
    if ((c & 0xf8) == 0xf0) { /* 4 byte char */
        *cpoint = (c & 0x07) << 18;
        cb = 3;
    }

    /* process the continuation bytes */
    while (cb--) {
        if ((*p & 0xc0) == 0x80)
            *cpoint |= (*p++ & 0x3f) << (cb * 6);
        else {
            *cpoint = 0xfffd;
            break;
        }
    }

    return (char *)p;
}


#endif /* XS_IMPLEMENTATION */

#endif /* _XS_UNICODE_H */
