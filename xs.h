/* copyright (c) 2022 grunfink - MIT license */

#ifndef _XS_H

#define _XS_H

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <unistd.h>
#include <stdarg.h>
#include <signal.h>
#include <errno.h>

typedef enum {
    XSTYPE_NULL   = 0x18,
    XSTYPE_TRUE   = 0x06,
    XSTYPE_FALSE  = 0x15,
    XSTYPE_LIST   = 0x11,
    XSTYPE_LITEM  = 0x1f,
    XSTYPE_EOL    = 0x12,
    XSTYPE_DICT   = 0x13,
    XSTYPE_DITEM  = 0x1e,
    XSTYPE_EOD    = 0x14,
    XSTYPE_NUMBER = 0x17,
    XSTYPE_STRING = 0x02
} xstype;


/* dynamic strings */
typedef char d_char;

/* auto-destroyable strings */
#define xs __attribute__ ((__cleanup__ (_xs_destroy))) d_char

/* not really all, just very much */
#define XS_ALL 0xfffffff

void *xs_free(void *ptr);
void *_xs_realloc(void *ptr, size_t size, const char *file, int line, const char *func);
#define xs_realloc(ptr, size) _xs_realloc(ptr, size, __FILE__, __LINE__, __FUNCTION__)
int _xs_blk_size(int sz);
void _xs_destroy(char **var);
#define xs_debug() raise(SIGTRAP)
xstype xs_type(const char *data);
int xs_size(const char *data);
int xs_is_null(const char *data);
d_char *xs_dup(const char *data);
d_char *xs_expand(d_char *data, int offset, int size);
d_char *xs_collapse(d_char *data, int offset, int size);
d_char *xs_insert_m(d_char *data, int offset, const char *mem, int size);
#define xs_insert(data, offset, data2) xs_insert_m(data, offset, data2, xs_size(data2))
#define xs_append_m(data, mem, size) xs_insert_m(data, xs_size(data) - 1, mem, size)
d_char *xs_str_new(const char *str);
#define xs_str_cat(str1, str2) xs_insert(str1, xs_size(str1) - 1, str2)
d_char *xs_replace_i(d_char *str, const char *sfrom, const char *sto);
#define xs_replace(str, sfrom, sto) xs_replace_i(xs_dup(str), sfrom, sto)
d_char *xs_fmt(const char *fmt, ...);
int xs_str_in(const char *haystack, const char *needle);
int xs_startswith(const char *str, const char *prefix);
int xs_endswith(const char *str, const char *postfix);
d_char *xs_crop(d_char *str, int start, int end);
d_char *xs_strip_chars(d_char *str, const char *chars);
#define xs_strip(str) xs_strip_chars(str, " \r\n\t\v\f")
d_char *xs_tolower(d_char *str);
d_char *xs_str_prepend(d_char *str, const char *prefix);
d_char *xs_list_new(void);
d_char *xs_list_append_m(d_char *list, const char *mem, int dsz);
#define xs_list_append(list, data) xs_list_append_m(list, data, xs_size(data))
int xs_list_iter(char **list, char **value);
int xs_list_len(char *list);
char *xs_list_get(char *list, int num);
d_char *xs_list_del(d_char *list, int num);
d_char *xs_list_insert(d_char *list, int num, const char *data);
d_char *xs_list_insert_sorted(d_char *list, const char *str);
d_char *xs_list_set(d_char *list, int num, const char *data);
d_char *xs_list_pop(d_char *list, char **data);
int xs_list_in(char *list, const char *val);
d_char *xs_join(char *list, const char *sep);
d_char *xs_split_n(const char *str, const char *sep, int times);
#define xs_split(str, sep) xs_split_n(str, sep, XS_ALL)
d_char *xs_dict_new(void);
d_char *xs_dict_append_m(d_char *dict, const char *key, const char *mem, int dsz);
#define xs_dict_append(dict, key, data) xs_dict_append_m(dict, key, data, xs_size(data))
int xs_dict_iter(char **dict, char **key, char **value);
char *xs_dict_get(char *dict, const char *key);
d_char *xs_dict_del(d_char *dict, const char *key);
d_char *xs_dict_set(d_char *dict, const char *key, const char *data);
d_char *xs_val_new(xstype t);
d_char *xs_number_new(double f);
double xs_number_get(const char *v);
const char *xs_number_str(const char *v);

void *xs_memmem(const char *haystack, int h_size, const char *needle, int n_size);

#ifdef XS_IMPLEMENTATION

void *_xs_realloc(void *ptr, size_t size, const char *file, int line, const char *func)
{
    d_char *ndata = realloc(ptr, size);

    if (ndata == NULL) {
        fprintf(stderr, "**OUT OF MEMORY**\n");
        abort();
    }

#ifdef XS_DEBUG
    if (ndata != ptr) {
        int n;
        FILE *f = fopen("xs_memory.out", "a");

        if (ptr != NULL)
            fprintf(f, "%p r\n", ptr);

        fprintf(f, "%p a %ld %s:%d: %s", ndata, size, file, line, func);

        if (ptr != NULL) {
            fprintf(f, " [");
            for (n = 0; n < 32 && ndata[n]; n++) {
                if (ndata[n] >= 32 && ndata[n] <= 127)
                    fprintf(f, "%c", ndata[n]);
                else
                    fprintf(f, "\\%02x", (unsigned char)ndata[n]);
            }
            fprintf(f, "]");
        }

        fprintf(f, "\n");

        fclose(f);
    }
#else
    (void)file;
    (void)line;
    (void)func;
#endif

    return ndata;
}


void *xs_free(void *ptr)
{
#ifdef XS_DEBUG
    if (ptr != NULL) {
        FILE *f = fopen("xs_memory.out", "a");
        fprintf(f, "%p b\n", ptr);
        fclose(f);
    }
#endif

    free(ptr);
    return NULL;
}


void _xs_destroy(char **var)
{
/*
    if (_xs_debug)
        printf("_xs_destroy %p\n", var);
*/
    xs_free(*var);
}


int _xs_blk_size(int sz)
/* calculates the block size */
{
    int blk_size = 4096;

    if (sz < 256)
        blk_size = 32;
    else
    if (sz < 4096)
        blk_size = 256;

    return ((((sz) + blk_size) / blk_size) * blk_size);
}


xstype xs_type(const char *data)
/* return the type of data */
{
    xstype t;

    switch (data[0]) {
    case XSTYPE_NULL:
    case XSTYPE_TRUE:
    case XSTYPE_FALSE:
    case XSTYPE_LIST:
    case XSTYPE_EOL:
    case XSTYPE_DICT:
    case XSTYPE_EOD:
    case XSTYPE_LITEM:
    case XSTYPE_DITEM:
    case XSTYPE_NUMBER:
        t = data[0];
        break;
    default:
        t = XSTYPE_STRING;
        break;
    }

    return t;
}


void _xs_put_24b(char *ptr, int i)
/* writes i as a 24 bit value */
{
    unsigned char *p = (unsigned char *)ptr;

    p[0] = (i >> 16) & 0xff;
    p[1] = (i >> 8) & 0xff;
    p[2] = i & 0xff;
}


int _xs_get_24b(const char *ptr)
/* reads a 24 bit value */
{
    unsigned char *p = (unsigned char *)ptr;

    return (p[0] << 16) | (p[1] << 8) | p[2];
}


int xs_size(const char *data)
/* returns the size of data in bytes */
{
    int len = 0;
    const char *p;

    if (data == NULL)
        return 0;

    switch (xs_type(data)) {
    case XSTYPE_STRING:
        len = strlen(data) + 1;
        break;

    case XSTYPE_LIST:
        len = _xs_get_24b(data + 1);

        break;

    case XSTYPE_DICT:
        len = _xs_get_24b(data + 1);

        break;

    case XSTYPE_DITEM:
        /* calculate the size of the key and the value */
        p = data + 1;
        p += xs_size(p);
        p += xs_size(p);

        len = p - data;

        break;

    case XSTYPE_LITEM:
        /* it's the size of the item + 1 */
        p = data + 1;
        p += xs_size(p);

        len = p - data;

        break;

    case XSTYPE_NUMBER:
        len = 1 + xs_size(data + 1);

        break;

    default:
        len = 1;
    }

    return len;
}


int xs_is_null(const char *data)
/* checks for null */
{
    return !!(data == NULL || xs_type(data) == XSTYPE_NULL);
}


d_char *xs_dup(const char *data)
/* creates a duplicate of data */
{
    int sz = xs_size(data);
    d_char *s = xs_realloc(NULL, _xs_blk_size(sz));

    memcpy(s, data, sz);

    return s;
}


d_char *xs_expand(d_char *data, int offset, int size)
/* opens a hole in data */
{
    int sz = xs_size(data);

    /* open room */
    if (sz == 0 || _xs_blk_size(sz) != _xs_blk_size(sz + size))
        data = xs_realloc(data, _xs_blk_size(sz + size));

    /* move up the rest of the data */
    if (data != NULL)
        memmove(data + offset + size, data + offset, sz - offset);

    if (xs_type(data) == XSTYPE_LIST || xs_type(data) == XSTYPE_DICT)
        _xs_put_24b(data + 1, sz + size);

    return data;
}


d_char *xs_collapse(d_char *data, int offset, int size)
/* shrinks data */
{
    int sz = xs_size(data);
    int n;

    /* don't try to delete beyond the limit */
    if (offset + size > sz)
        size = sz - offset;

    /* shrink total size */
    sz -= size;

    for (n = offset; n < sz; n++)
        data[n] = data[n + size];

    if (xs_type(data) == XSTYPE_LIST || xs_type(data) == XSTYPE_DICT)
        _xs_put_24b(data + 1, sz);

    return xs_realloc(data, _xs_blk_size(sz));
}


d_char *xs_insert_m(d_char *data, int offset, const char *mem, int size)
/* inserts a memory block */
{
    data = xs_expand(data, offset, size);
    memcpy(data + offset, mem, size);

    return data;
}


/** strings **/

d_char *xs_str_new(const char *str)
/* creates a new string */
{
    return xs_insert(NULL, 0, str ? str : "");
}


d_char *xs_replace_i(d_char *str, const char *sfrom, const char *sto)
/* replaces inline all sfrom with sto */
{
    int sfsz = strlen(sfrom);
    int stsz = strlen(sto);
    char *ss;
    int offset = 0;

    while ((ss = strstr(str + offset, sfrom)) != NULL) {
        int n_offset = ss - str;

        str = xs_collapse(str, n_offset, sfsz);
        str = xs_expand(str, n_offset, stsz);
        memcpy(str + n_offset, sto, stsz);

        offset = n_offset + stsz;
    }

    return str;
}


d_char *xs_fmt(const char *fmt, ...)
/* formats a string with printf()-like marks */
{
    int n;
    d_char *s = NULL;
    va_list ap;

    va_start(ap, fmt);
    n = vsnprintf(s, 0, fmt, ap);
    va_end(ap);

    if (n > 0) {
        s = xs_realloc(NULL, _xs_blk_size(n + 1));

        va_start(ap, fmt);
        vsnprintf(s, n + 1, fmt, ap);
        va_end(ap);
    }

    return s;
}


int xs_str_in(const char *haystack, const char *needle)
/* finds needle in haystack and returns the offset or -1 */
{
    char *s;
    int r = -1;

    if ((s = strstr(haystack, needle)) != NULL)
        r = s - haystack;

    return r;
}


int xs_startswith(const char *str, const char *prefix)
/* returns true if str starts with prefix */
{
    return !!(xs_str_in(str, prefix) == 0);
}


int xs_endswith(const char *str, const char *postfix)
/* returns true if str ends with postfix */
{
    int ssz = strlen(str);
    int psz = strlen(postfix);

    return !!(ssz >= psz && memcmp(postfix, str + ssz - psz, psz) == 0);
}


d_char *xs_crop(d_char *str, int start, int end)
/* crops the d_char to be only from start to end */
{
    int sz = strlen(str);

    if (end <= 0)
        end = sz + end;

    /* crop from the top */
    str[end] = '\0';

    /* crop from the bottom */
    str = xs_collapse(str, 0, start);

    return str;
}


d_char *xs_strip_chars(d_char *str, const char *chars)
/* strips the string of chars from the start and the end */
{
    int n;

    /* strip first from the end */
    for (n = strlen(str); n > 0 && strchr(chars, str[n - 1]); n--);
    str[n] = '\0';

    if (str[0]) {
        /* now strip from the beginning */
        for (n = 0; str[n] && strchr(chars, str[n]); n++);

        if (n)
            str = xs_collapse(str, 0, n);
    }

    return str;
}


d_char *xs_tolower(d_char *str)
/* convert to lowercase */
{
    int n;

    for (n = 0; str[n]; n++)
        str[n] = tolower(str[n]);

    return str;
}


d_char *xs_str_prepend(d_char *str, const char *prefix)
/* prepends prefix into string */
{
    int sz = strlen(prefix);

    str = xs_expand(str, 0, sz);
    memcpy(str, prefix, sz);

    return str;
}


/** lists **/

d_char *xs_list_new(void)
/* creates a new list */
{
    d_char *list;

    list = xs_realloc(NULL, _xs_blk_size(5));
    list[0] = XSTYPE_LIST;
    list[4] = XSTYPE_EOL;

    _xs_put_24b(list + 1, 5);

    return list;
}


d_char *_xs_list_write_litem(d_char *list, int offset, const char *mem, int dsz)
/* writes a list item */
{
    char c = XSTYPE_LITEM;

    list = xs_insert_m(list, offset,     &c,  1);
    list = xs_insert_m(list, offset + 1, mem, dsz);

    return list;
}


d_char *xs_list_append_m(d_char *list, const char *mem, int dsz)
/* adds a memory block to the list */
{
    return _xs_list_write_litem(list, xs_size(list) - 1, mem, dsz);
}


int xs_list_iter(char **list, char **value)
/* iterates a list value */
{
    int goon = 1;
    char *p;

    if (list == NULL || *list == NULL)
        return 0;

    p = *list;

    /* skip the start of the list */
    if (*p == XSTYPE_LIST)
        p += 4;

    /* an element? */
    if (*p == XSTYPE_LITEM) {
        p++;

        *value = p;

        p += xs_size(*value);
    }
    else {
        /* end of list */
        p++;
        goon = 0;
    }

    /* store back the pointer */
    *list = p;

    return goon;
}


int xs_list_len(char *list)
/* returns the number of elements in the list */
{
    int c = 0;
    char *v;

    while (xs_list_iter(&list, &v))
        c++;

    return c;
}


char *xs_list_get(char *list, int num)
/* returns the element #num */
{
    char *v;
    int c = 0;

    if (num < 0)
        num = xs_list_len(list) + num;

    while (xs_list_iter(&list, &v)) {
        if (c == num)
            return v;

        c++;
    }

    return NULL;
}


d_char *xs_list_del(d_char *list, int num)
/* deletes element #num */
{
    char *v;

    if ((v = xs_list_get(list, num)) != NULL)
        list = xs_collapse(list, v - 1 - list, xs_size(v - 1));

    return list;
}


d_char *xs_list_insert(d_char *list, int num, const char *data)
/* inserts an element at #num position */
{
    char *v;
    int offset;

    if ((v = xs_list_get(list, num)) != NULL)
        offset = v - list;
    else
        offset = xs_size(list);

    return _xs_list_write_litem(list, offset - 1, data, xs_size(data));
}


d_char *xs_list_insert_sorted(d_char *list, const char *str)
/* inserts a string in the list in its ordered position */
{
    char *p, *v;
    int offset = xs_size(list);

    p = list;
    while (xs_list_iter(&p, &v)) {
        /* if this element is greater or equal, insert here */
        if (strcmp(v, str) >= 0) {
            offset = v - list;
            break;
        }
    }

    return _xs_list_write_litem(list, offset - 1, str, xs_size(str));
}


d_char *xs_list_set(d_char *list, int num, const char *data)
/* sets the element at #num position */
{
    list = xs_list_del(list, num);
    list = xs_list_insert(list, num, data);

    return list;
}


d_char *xs_list_pop(d_char *list, char **data)
/* pops the last element from the list */
{
    if ((*data = xs_list_get(list, -1)) != NULL) {
        *data = xs_dup(*data);
        list = xs_list_del(list, -1);
    }

    return list;
}


int xs_list_in(char *list, const char *val)
/* returns the position of val in list or -1 */
{
    int n = 0;
    char *v;
    int sz = xs_size(val);

    while (xs_list_iter(&list, &v)) {
        if (sz == xs_size(v) && memcmp(val, v, sz) == 0)
            return n;

        n++;
    }

    return -1;
}


d_char *xs_join(char *list, const char *sep)
/* joins a list into a string */
{
    d_char *s = NULL;
    char *v;
    int c = 0;
    int offset = 0;
    int ssz = strlen(sep);

    while (xs_list_iter(&list, &v)) {
        /* refuse to join non-string values */
        if (xs_type(v) == XSTYPE_STRING) {
            int sz;

            /* add the separator */
            if (c != 0) {
                s = xs_realloc(s, offset + ssz);
                memcpy(s + offset, sep, ssz);
                offset += ssz;
            }

            /* add the element */
            sz = strlen(v);
            s = xs_realloc(s, offset + sz);
            memcpy(s + offset, v, sz);
            offset += sz;

            c++;
        }
    }

    /* null-terminate */
    s = xs_realloc(s, _xs_blk_size(offset + 1));
    s[offset] = '\0';

    return s;
}


d_char *xs_split_n(const char *str, const char *sep, int times)
/* splits a string into a list upto n times */
{
    int sz = strlen(sep);
    char *ss;
    d_char *list;

    list = xs_list_new();

    while (times > 0 && (ss = strstr(str, sep)) != NULL) {
        /* add the first part (without the asciiz) */
        list = xs_list_append_m(list, str, ss - str);

        /* add the asciiz */
        list = xs_str_cat(list, "");

        /* skip past the separator */
        str = ss + sz;

        times--;
    }

    /* add the rest of the string */
    list = xs_list_append(list, str);

    return list;
}


/** dicts **/

d_char *xs_dict_new(void)
/* creates a new dict */
{
    d_char *dict;

    dict = xs_realloc(NULL, _xs_blk_size(5));
    dict[0] = XSTYPE_DICT;
    dict[4] = XSTYPE_EOD;

    _xs_put_24b(dict + 1, 5);

    return dict;
}


d_char *xs_dict_append_m(d_char *dict, const char *key, const char *mem, int dsz)
/* adds a memory block to the dict */
{
    char c = XSTYPE_DITEM;
    int sz = xs_size(dict);
    int ksz = xs_size(key);

    dict = xs_insert_m(dict, sz - 1, &c, 1);
    dict = xs_insert_m(dict, sz, key, ksz);
    dict = xs_insert_m(dict, sz + ksz, mem, dsz);

    return dict;
}


int xs_dict_iter(char **dict, char **key, char **value)
/* iterates a dict value */
{
    int goon = 1;
    char *p;

    if (dict == NULL || *dict == NULL)
        return 0;

    p = *dict;

    /* skip the start of the list */
    if (*p == XSTYPE_DICT)
        p += 4;

    /* an element? */
    if (*p == XSTYPE_DITEM) {
        p++;

        *key = p;
        p += xs_size(*key);

        *value = p;
        p += xs_size(*value);
    }
    else {
        /* end of list */
        p++;
        goon = 0;
    }

    /* store back the pointer */
    *dict = p;

    return goon;
}


char *xs_dict_get(char *dict, const char *key)
/* returns the value directed by key */
{
    char *k, *v;

    while (xs_dict_iter(&dict, &k, &v)) {
        if (strcmp(k, key) == 0)
            return v;
    }

    return NULL;
}


d_char *xs_dict_del(d_char *dict, const char *key)
/* deletes a key */
{
    char *k, *v;
    char *p = dict;

    while (xs_dict_iter(&p, &k, &v)) {
        if (strcmp(k, key) == 0) {
            /* the address of the item is just behind the key */
            char *i = k - 1;

            dict = xs_collapse(dict, i - dict, xs_size(i));
            break;
        }
    }

    return dict;
}


d_char *xs_dict_set(d_char *dict, const char *key, const char *data)
/* sets (replaces) a key */
{
    /* delete the possibly existing key */
    dict = xs_dict_del(dict, key);

    /* append the data */
    dict = xs_dict_append(dict, key, data);

    return dict;
}


/** other values **/

d_char *xs_val_new(xstype t)
/* adds a new special value */
{
    d_char *v = xs_realloc(NULL, _xs_blk_size(1));

    v[0] = t;

    return v;
}


/** numbers */

d_char *xs_number_new(double f)
/* adds a new number value */
{
    d_char *v;
    char tmp[64];

    snprintf(tmp, sizeof(tmp), "%.15lf", f);

    /* strip useless zeros */
    if (strchr(tmp, '.') != NULL) {
        char *ptr;

        for (ptr = tmp + strlen(tmp) - 1; *ptr == '0'; ptr--);

        if (*ptr != '.')
            ptr++;

        *ptr = '\0';
    }

    /* alloc for the marker and the full string */
    v = xs_realloc(NULL, _xs_blk_size(1 + xs_size(tmp)));

    v[0] = XSTYPE_NUMBER;
    memcpy(&v[1], tmp, xs_size(tmp));

    return v;
}


double xs_number_get(const char *v)
/* gets the number as a double */
{
    double f = 0.0;

    if (v != NULL && v[0] == XSTYPE_NUMBER)
        f = atof(&v[1]);

    return f;
}


const char *xs_number_str(const char *v)
/* gets the number as a string */
{
    const char *p = NULL;

    if (v != NULL && v[0] == XSTYPE_NUMBER)
        p = &v[1];

    return p;
}


void *xs_memmem(const char *haystack, int h_size, const char *needle, int n_size)
/* clone of memmem */
{
    char *p, *r = NULL;
    int offset = 0;

    while (!r && h_size - offset > n_size &&
           (p = memchr(haystack + offset, *needle, h_size - offset))) {
        if (memcmp(p, needle, n_size) == 0)
            r = p;
        else
            offset = p - haystack + 1;
    }

    return r;
}


#endif /* XS_IMPLEMENTATION */

#endif /* _XS_H */
