/* copyright (c) 2022 - 2023 grunfink / MIT license */

#ifndef _XS_UNICODE_H

#define _XS_UNICODE_H

 xs_str *xs_utf8_enc(xs_str *str, unsigned int cpoint);


#ifdef XS_IMPLEMENTATION

/** utf-8 **/

xs_str *xs_utf8_enc(xs_str *str, unsigned int cpoint)
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

#endif /* _XS_UNICODE_H */
