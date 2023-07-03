/* copyright (c) 2022 - 2023 grunfink / MIT license */

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
    XSTYPE_STRING = 0x02,       /* C string (\0 delimited) (NOT STORED) */
    XSTYPE_NUMBER = 0x17,       /* double in spirit, stored as a C string (\0 delimited) */
    XSTYPE_NULL   = 0x18,       /* Special NULL value */
    XSTYPE_TRUE   = 0x06,       /* Boolean */
    XSTYPE_FALSE  = 0x15,       /* Boolean */
    XSTYPE_LIST   = 0x1d,       /* Sequence of LITEMs up to EOM (with 24bit size) */
    XSTYPE_LITEM  = 0x1f,       /* Element of a list (any type) */
    XSTYPE_DICT   = 0x1c,       /* Sequence of DITEMs up to EOM (with 24bit size) */
    XSTYPE_DITEM  = 0x1e,       /* Element of a dict (STRING key + any type) */
    XSTYPE_EOM    = 0x19,       /* End of Multiple (LIST or DICT) */
    XSTYPE_DATA   = 0x10        /* A block of anonymous data */
} xstype;


/* dynamic strings */
typedef char d_char;

/* types */
typedef char xs_val;
typedef char xs_str;
typedef char xs_list;
typedef char xs_dict;
typedef char xs_number;
typedef char xs_data;

/* auto-destroyable strings */
#define xs __attribute__ ((__cleanup__ (_xs_destroy))) xs_val

/* not really all, just very much */
#define XS_ALL 0xfffffff

void *xs_free(void *ptr);
void *_xs_realloc(void *ptr, size_t size, const char *file, int line, const char *func);
#define xs_realloc(ptr, size) _xs_realloc(ptr, size, __FILE__, __LINE__, __FUNCTION__)
int _xs_blk_size(int sz);
void _xs_destroy(char **var);
#define xs_debug() raise(SIGTRAP)
xstype xs_type(const xs_val *data);
int xs_size(const xs_val *data);
int xs_is_null(const xs_val *data);
xs_val *xs_dup(const xs_val *data);
xs_val *xs_expand(xs_val *data, int offset, int size);
xs_val *xs_collapse(xs_val *data, int offset, int size);
xs_val *xs_insert_m(xs_val *data, int offset, const char *mem, int size);
#define xs_insert(data, offset, data2) xs_insert_m(data, offset, data2, xs_size(data2))
#define xs_append_m(data, mem, size) xs_insert_m(data, xs_size(data) - 1, mem, size)

xs_str *xs_str_new(const char *str);
xs_str *xs_str_wrap_i(const char *prefix, xs_str *str, const char *suffix);
#define xs_str_prepend_i(str, prefix) xs_str_wrap_i(prefix, str, NULL)
#define xs_str_cat(str, suffix) xs_str_wrap_i(NULL, str, suffix)
xs_str *xs_replace_in(xs_str *str, const char *sfrom, const char *sto, int times);
#define xs_replace_i(str, sfrom, sto) xs_replace_in(str, sfrom, sto, XS_ALL)
#define xs_replace(str, sfrom, sto) xs_replace_in(xs_dup(str), sfrom, sto, XS_ALL)
#define xs_replace_n(str, sfrom, sto, times) xs_replace_in(xs_dup(str), sfrom, sto, times)
xs_str *xs_fmt(const char *fmt, ...);
int xs_str_in(const char *haystack, const char *needle);
int _xs_startsorends(const char *str, const char *xfix, int ends);
#define xs_startswith(str, prefix) _xs_startsorends(str, prefix, 0)
#define xs_endswith(str, postfix) _xs_startsorends(str, postfix, 1)
xs_str *xs_crop_i(xs_str *str, int start, int end);
xs_str *xs_strip_chars_i(xs_str *str, const char *chars);
#define xs_strip_i(str) xs_strip_chars_i(str, " \r\n\t\v\f")
xs_str *xs_tolower_i(xs_str *str);

xs_list *xs_list_new(void);
xs_list *xs_list_append_m(xs_list *list, const char *mem, int dsz);
#define xs_list_append(list, data) xs_list_append_m(list, data, xs_size(data))
int xs_list_iter(xs_list **list, xs_val **value);
int xs_list_len(const xs_list *list);
xs_val *xs_list_get(const xs_list *list, int num);
xs_list *xs_list_del(xs_list *list, int num);
xs_list *xs_list_insert(xs_list *list, int num, const xs_val *data);
xs_list *xs_list_insert_sorted(xs_list *list, const char *str);
xs_list *xs_list_set(xs_list *list, int num, const xs_val *data);
xs_list *xs_list_dequeue(xs_list *list, xs_val **data, int last);
#define xs_list_pop(list, data) xs_list_dequeue(list, data, 1)
#define xs_list_shift(list, data) xs_list_dequeue(list, data, 0)
int xs_list_in(const xs_list *list, const xs_val *val);
xs_str *xs_join(const xs_list *list, const char *sep);
xs_list *xs_split_n(const char *str, const char *sep, int times);
#define xs_split(str, sep) xs_split_n(str, sep, XS_ALL)

xs_dict *xs_dict_new(void);
xs_dict *xs_dict_append_m(xs_dict *dict, const xs_str *key, const xs_val *mem, int dsz);
#define xs_dict_append(dict, key, data) xs_dict_append_m(dict, key, data, xs_size(data))
xs_dict *xs_dict_prepend_m(xs_dict *dict, const xs_str *key, const xs_val *mem, int dsz);
#define xs_dict_prepend(dict, key, data) xs_dict_prepend_m(dict, key, data, xs_size(data))
int xs_dict_iter(xs_dict **dict, xs_str **key, xs_val **value);
xs_val *xs_dict_get(const xs_dict *dict, const xs_str *key);
xs_dict *xs_dict_del(xs_dict *dict, const xs_str *key);
xs_dict *xs_dict_set(xs_dict *dict, const xs_str *key, const xs_val *data);

xs_val *xs_val_new(xstype t);
xs_number *xs_number_new(double f);
double xs_number_get(const xs_number *v);
const char *xs_number_str(const xs_number *v);

xs_data *xs_data_new(const void *data, int size);
int xs_data_size(const xs_data *value);
void xs_data_get(const xs_data *value, void *data);

void *xs_memmem(const char *haystack, int h_size, const char *needle, int n_size);

xs_str *xs_hex_enc(const xs_val *data, int size);
xs_val *xs_hex_dec(const xs_str *hex, int *size);
int xs_is_hex(const char *str);

unsigned int xs_hash_func(const char *data, int size);

#ifdef XS_ASSERT
#include <assert.h>
#define XS_ASSERT_TYPE(v, t) assert(xs_type(v) == t)
#define XS_ASSERT_TYPE_NULL(v, t) assert(v == NULL || xs_type(v) == t)
#else
#define XS_ASSERT_TYPE(v, t) (void)(0)
#define XS_ASSERT_TYPE_NULL(v, t) (void)(0)
#endif

extern xs_val xs_stock_null[];
extern xs_val xs_stock_true[];
extern xs_val xs_stock_false[];

#define xs_return(v) xs_val *__r = v; v = NULL; return __r


#ifdef XS_IMPLEMENTATION

xs_val xs_stock_null[]  = { XSTYPE_NULL };
xs_val xs_stock_true[]  = { XSTYPE_TRUE };
xs_val xs_stock_false[] = { XSTYPE_FALSE };


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


xstype xs_type(const xs_val *data)
/* return the type of data */
{
    xstype t;

    if (data == NULL)
        t = XSTYPE_NULL;
    else
    switch (data[0]) {
    case XSTYPE_NULL:
    case XSTYPE_TRUE:
    case XSTYPE_FALSE:
    case XSTYPE_LIST:
    case XSTYPE_LITEM:
    case XSTYPE_DICT:
    case XSTYPE_DITEM:
    case XSTYPE_NUMBER:
    case XSTYPE_EOM:
    case XSTYPE_DATA:
        t = data[0];
        break;
    default:
        t = XSTYPE_STRING;
        break;
    }

    return t;
}


void _xs_put_24b(xs_val *ptr, int i)
/* writes i as a 24 bit value */
{
    unsigned char *p = (unsigned char *)ptr;

    p[0] = (i >> 16) & 0xff;
    p[1] = (i >> 8) & 0xff;
    p[2] = i & 0xff;
}


int _xs_get_24b(const xs_val *ptr)
/* reads a 24 bit value */
{
    unsigned char *p = (unsigned char *)ptr;

    return (p[0] << 16) | (p[1] << 8) | p[2];
}


int xs_size(const xs_val *data)
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
    case XSTYPE_DICT:
    case XSTYPE_DATA:
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


int xs_is_null(const xs_val *data)
/* checks for null */
{
    return (xs_type(data) == XSTYPE_NULL);
}


xs_val *xs_dup(const xs_val *data)
/* creates a duplicate of data */
{
    int sz = xs_size(data);
    xs_val *s = xs_realloc(NULL, _xs_blk_size(sz));

    memcpy(s, data, sz);

    return s;
}


xs_val *xs_expand(xs_val *data, int offset, int size)
/* opens a hole in data */
{
    int sz = xs_size(data);

    /* open room */
    if (sz == 0 || _xs_blk_size(sz) != _xs_blk_size(sz + size))
        data = xs_realloc(data, _xs_blk_size(sz + size));

    /* move up the rest of the data */
    if (data != NULL)
        memmove(data + offset + size, data + offset, sz - offset);

    if (xs_type(data) == XSTYPE_LIST ||
        xs_type(data) == XSTYPE_DICT ||
        xs_type(data) == XSTYPE_DATA)
        _xs_put_24b(data + 1, sz + size);

    return data;
}


xs_val *xs_collapse(xs_val *data, int offset, int size)
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

    if (xs_type(data) == XSTYPE_LIST ||
        xs_type(data) == XSTYPE_DICT ||
        xs_type(data) == XSTYPE_DATA)
        _xs_put_24b(data + 1, sz);

    return xs_realloc(data, _xs_blk_size(sz));
}


xs_val *xs_insert_m(xs_val *data, int offset, const char *mem, int size)
/* inserts a memory block */
{
    data = xs_expand(data, offset, size);
    memcpy(data + offset, mem, size);

    return data;
}


/** strings **/

xs_str *xs_str_new(const char *str)
/* creates a new string */
{
    return xs_insert(NULL, 0, str ? str : "");
}


xs_str *xs_str_wrap_i(const char *prefix, xs_str *str, const char *suffix)
/* wraps str with prefix and suffix */
{
    XS_ASSERT_TYPE(str, XSTYPE_STRING);

    if (prefix)
        str = xs_insert_m(str, 0, prefix, strlen(prefix));

    if (suffix)
        str = xs_insert_m(str, xs_size(str) - 1, suffix, xs_size(suffix));

    return str;
}


xs_str *xs_replace_in(xs_str *str, const char *sfrom, const char *sto, int times)
/* replaces inline all sfrom with sto */
{
    XS_ASSERT_TYPE(str, XSTYPE_STRING);

    int sfsz = strlen(sfrom);
    int stsz = strlen(sto);
    char *ss;
    int offset = 0;

    while (times > 0 && (ss = strstr(str + offset, sfrom)) != NULL) {
        int n_offset = ss - str;

        str = xs_collapse(str, n_offset, sfsz);
        str = xs_expand(str, n_offset, stsz);
        memcpy(str + n_offset, sto, stsz);

        offset = n_offset + stsz;

        times--;
    }

    return str;
}


xs_str *xs_fmt(const char *fmt, ...)
/* formats a string with printf()-like marks */
{
    int n;
    xs_str *s = NULL;
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


int _xs_startsorends(const char *str, const char *xfix, int ends)
/* returns true if str starts or ends with xfix */
{
    int ssz = strlen(str);
    int psz = strlen(xfix);

    return !!(ssz >= psz && memcmp(xfix, str + (ends ? ssz - psz : 0), psz) == 0);
}


xs_str *xs_crop_i(xs_str *str, int start, int end)
/* crops the d_char to be only from start to end */
{
    XS_ASSERT_TYPE(str, XSTYPE_STRING);

    int sz = strlen(str);

    if (end <= 0)
        end = sz + end;

    /* crop from the top */
    str[end] = '\0';

    /* crop from the bottom */
    str = xs_collapse(str, 0, start);

    return str;
}


xs_str *xs_strip_chars_i(xs_str *str, const char *chars)
/* strips the string of chars from the start and the end */
{
    XS_ASSERT_TYPE(str, XSTYPE_STRING);

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


xs_str *xs_tolower_i(xs_str *str)
/* convert to lowercase */
{
    XS_ASSERT_TYPE(str, XSTYPE_STRING);

    int n;

    for (n = 0; str[n]; n++)
        str[n] = tolower(str[n]);

    return str;
}


/** lists **/

xs_list *xs_list_new(void)
/* creates a new list */
{
    xs_list *list;

    list = xs_realloc(NULL, _xs_blk_size(5));
    list[0] = XSTYPE_LIST;
    list[4] = XSTYPE_EOM;

    _xs_put_24b(list + 1, 5);

    return list;
}


xs_list *_xs_list_write_litem(xs_list *list, int offset, const char *mem, int dsz)
/* writes a list item */
{
    XS_ASSERT_TYPE(list, XSTYPE_LIST);

    char c = XSTYPE_LITEM;

    list = xs_insert_m(list, offset,     &c,  1);
    list = xs_insert_m(list, offset + 1, mem, dsz);

    return list;
}


xs_list *xs_list_append_m(xs_list *list, const char *mem, int dsz)
/* adds a memory block to the list */
{
    XS_ASSERT_TYPE(list, XSTYPE_LIST);

    return _xs_list_write_litem(list, xs_size(list) - 1, mem, dsz);
}


int xs_list_iter(xs_list **list, xs_val **value)
/* iterates a list value */
{
    int goon = 1;

    xs_val *p = *list;

    /* skip the start of the list */
    if (xs_type(p) == XSTYPE_LIST)
        p += 4;

    /* an element? */
    if (xs_type(p) == XSTYPE_LITEM) {
        p++;

        *value = p;

        p += xs_size(*value);
    }
    else {
        /* end of list */
        goon = 0;
    }

    /* store back the pointer */
    *list = p;

    return goon;
}


int xs_list_len(const xs_list *list)
/* returns the number of elements in the list */
{
    XS_ASSERT_TYPE_NULL(list, XSTYPE_LIST);

    int c = 0;
    xs_list *p = (xs_list *)list;
    xs_val *v;

    while (xs_list_iter(&p, &v))
        c++;

    return c;
}


xs_val *xs_list_get(const xs_list *list, int num)
/* returns the element #num */
{
    XS_ASSERT_TYPE(list, XSTYPE_LIST);

    if (num < 0)
        num = xs_list_len(list) + num;

    int c = 0;
    xs_list *p = (xs_list *)list;
    xs_val *v;

    while (xs_list_iter(&p, &v)) {
        if (c == num)
            return v;

        c++;
    }

    return NULL;
}


xs_list *xs_list_del(xs_list *list, int num)
/* deletes element #num */
{
    XS_ASSERT_TYPE(list, XSTYPE_LIST);

    xs_val *v;

    if ((v = xs_list_get(list, num)) != NULL)
        list = xs_collapse(list, v - 1 - list, xs_size(v - 1));

    return list;
}


xs_list *xs_list_insert(xs_list *list, int num, const xs_val *data)
/* inserts an element at #num position */
{
    XS_ASSERT_TYPE(list, XSTYPE_LIST);

    xs_val *v;
    int offset;

    if ((v = xs_list_get(list, num)) != NULL)
        offset = v - list;
    else
        offset = xs_size(list);

    return _xs_list_write_litem(list, offset - 1, data, xs_size(data));
}


xs_list *xs_list_insert_sorted(xs_list *list, const xs_str *str)
/* inserts a string in the list in its ordered position */
{
    XS_ASSERT_TYPE(list, XSTYPE_LIST);
    XS_ASSERT_TYPE(str, XSTYPE_STRING);

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


xs_list *xs_list_set(xs_list *list, int num, const xs_val *data)
/* sets the element at #num position */
{
    XS_ASSERT_TYPE(list, XSTYPE_LIST);

    list = xs_list_del(list, num);
    list = xs_list_insert(list, num, data);

    return list;
}


xs_list *xs_list_dequeue(xs_list *list, xs_val **data, int last)
/* gets a copy of the first or last element of a list, shrinking it */
{
    XS_ASSERT_TYPE(list, XSTYPE_LIST);

    xs_list *p = list;
    xs_val *v  = NULL;

    if (!last) {
        /* get the first */
        xs_list_iter(&p, &v);
    }
    else {
        /* iterate to the end */
        while (xs_list_iter(&p, &v));
    }

    if (v != NULL) {
        *data = xs_dup(v);

        /* collapse from the address of the element */
        list = xs_collapse(list, v - 1 - list, xs_size(v - 1));
    }

    return list;
}


int xs_list_in(const xs_list *list, const xs_val *val)
/* returns the position of val in list or -1 */
{
    XS_ASSERT_TYPE_NULL(list, XSTYPE_LIST);

    int n = 0;
    xs_list *p = (xs_list *)list;
    xs_val *v;
    int sz = xs_size(val);

    while (xs_list_iter(&p, &v)) {
        if (sz == xs_size(v) && memcmp(val, v, sz) == 0)
            return n;

        n++;
    }

    return -1;
}


xs_str *xs_join(const xs_list *list, const char *sep)
/* joins a list into a string */
{
    XS_ASSERT_TYPE(list, XSTYPE_LIST);

    xs_str *s = NULL;
    xs_list *p = (xs_list *)list;
    xs_val *v;
    int c = 0;
    int offset = 0;
    int ssz = strlen(sep);

    while (xs_list_iter(&p, &v)) {
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


xs_list *xs_split_n(const char *str, const char *sep, int times)
/* splits a string into a list upto n times */
{
    int sz = strlen(sep);
    char *ss;
    xs_list *list;

    list = xs_list_new();

    while (times > 0 && (ss = strstr(str, sep)) != NULL) {
        /* add the first part (without the asciiz) */
        list = xs_list_append_m(list, str, ss - str);

        /* add the asciiz */
        list = xs_insert_m(list, xs_size(list) - 1, "", 1);

        /* skip past the separator */
        str = ss + sz;

        times--;
    }

    /* add the rest of the string */
    list = xs_list_append(list, str);

    return list;
}


/** dicts **/

xs_dict *xs_dict_new(void)
/* creates a new dict */
{
    xs_dict *dict;

    dict = xs_realloc(NULL, _xs_blk_size(5));
    dict[0] = XSTYPE_DICT;
    dict[4] = XSTYPE_EOM;

    _xs_put_24b(dict + 1, 5);

    return dict;
}


xs_dict *xs_dict_insert_m(xs_dict *dict, int offset, const xs_str *key,
                          const xs_val *data, int dsz)
/* inserts a memory block into the dict */
{
    XS_ASSERT_TYPE(dict, XSTYPE_DICT);
    XS_ASSERT_TYPE(key, XSTYPE_STRING);

    int ksz = xs_size(key);

    dict = xs_expand(dict, offset, 1 + ksz + dsz);

    dict[offset] = XSTYPE_DITEM;
    memcpy(&dict[offset + 1], key, ksz);
    memcpy(&dict[offset + 1 + ksz], data, dsz);

    return dict;
}


xs_dict *xs_dict_append_m(xs_dict *dict, const xs_str *key, const xs_val *mem, int dsz)
/* appends a memory block to the dict */
{
    return xs_dict_insert_m(dict, xs_size(dict) - 1, key, mem, dsz);
}


xs_dict *xs_dict_prepend_m(xs_dict *dict, const xs_str *key, const xs_val *mem, int dsz)
/* prepends a memory block to the dict */
{
    return xs_dict_insert_m(dict, 4, key, mem, dsz);
}


int xs_dict_iter(xs_dict **dict, xs_str **key, xs_val **value)
/* iterates a dict value */
{
    int goon = 1;

    xs_val *p = *dict;

    /* skip the start of the list */
    if (xs_type(p) == XSTYPE_DICT)
        p += 4;

    /* an element? */
    if (xs_type(p) == XSTYPE_DITEM) {
        p++;

        *key = p;
        p += xs_size(*key);

        *value = p;
        p += xs_size(*value);
    }
    else {
        /* end of list */
        goon = 0;
    }

    /* store back the pointer */
    *dict = p;

    return goon;
}


xs_val *xs_dict_get(const xs_dict *dict, const xs_str *key)
/* returns the value directed by key */
{
    XS_ASSERT_TYPE(dict, XSTYPE_DICT);
    XS_ASSERT_TYPE(key, XSTYPE_STRING);

    xs_dict *p = (xs_dict *)dict;
    xs_str *k;
    xs_val *v;

    while (xs_dict_iter(&p, &k, &v)) {
        if (strcmp(k, key) == 0)
            return v;
    }

    return NULL;
}


xs_dict *xs_dict_del(xs_dict *dict, const xs_str *key)
/* deletes a key */
{
    XS_ASSERT_TYPE(dict, XSTYPE_DICT);
    XS_ASSERT_TYPE(key, XSTYPE_STRING);

    xs_str *k;
    xs_val *v;
    xs_dict *p = dict;

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


xs_dict *xs_dict_set(xs_dict *dict, const xs_str *key, const xs_val *data)
/* sets (replaces) a key */
{
    XS_ASSERT_TYPE(dict, XSTYPE_DICT);
    XS_ASSERT_TYPE(key, XSTYPE_STRING);

    /* delete the possibly existing key */
    dict = xs_dict_del(dict, key);

    /* add the data */
    dict = xs_dict_prepend(dict, key, data);

    return dict;
}


/** other values **/

xs_val *xs_val_new(xstype t)
/* adds a new special value */
{
    xs_val *v = xs_realloc(NULL, _xs_blk_size(1));

    v[0] = t;

    return v;
}


/** numbers */

xs_number *xs_number_new(double f)
/* adds a new number value */
{
    xs_number *v;
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


double xs_number_get(const xs_number *v)
/* gets the number as a double */
{
    double f = 0.0;

    if (v != NULL && v[0] == XSTYPE_NUMBER)
        f = atof(&v[1]);

    return f;
}


const char *xs_number_str(const xs_number *v)
/* gets the number as a string */
{
    const char *p = NULL;

    if (v != NULL && v[0] == XSTYPE_NUMBER)
        p = &v[1];

    return p;
}


/** raw data blocks **/

xs_data *xs_data_new(const void *data, int size)
/* returns a new raw data value */
{
    xs_data *v;

    /* add the overhead (data type + 24bit size) */
    int total_size = size + 4;

    v = xs_realloc(NULL, _xs_blk_size(total_size));
    v[0] = XSTYPE_DATA;

    _xs_put_24b(v + 1, total_size);

    memcpy(&v[4], data, size);

    return v;
}


int xs_data_size(const xs_data *value)
/* returns the size of the data stored inside value */
{
    return _xs_get_24b(value + 1) - 4;
}


void xs_data_get(const xs_data *value, void *data)
/* copies the raw data stored inside value into data */
{
    int size = _xs_get_24b(value + 1) - 4;
    memcpy(data, &value[4], size);
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


/** hex **/

xs_str *xs_hex_enc(const xs_val *data, int size)
/* returns an hexdump of data */
{
    xs_str *s;
    char *p;
    int n;

    p = s = xs_realloc(NULL, _xs_blk_size(size * 2 + 1));

    for (n = 0; n < size; n++) {
        snprintf(p, 3, "%02x", (unsigned char)data[n]);
        p += 2;
    }

    *p = '\0';

    return s;
}


xs_val *xs_hex_dec(const xs_str *hex, int *size)
/* decodes an hexdump into data */
{
    int sz = strlen(hex);
    xs_val *s = NULL;
    char *p;
    int n;

    if (sz % 2)
        return NULL;

    p = s = xs_realloc(NULL, _xs_blk_size(sz / 2 + 1));

    for (n = 0; n < sz; n += 2) {
        int i;
        if (sscanf(&hex[n], "%02x", &i) == 0) {
            /* decoding error */
            return xs_free(s);
        }
        else
            *p = i;

        p++;
    }

    *p = '\0';
    *size = sz / 2;

    return s;
}


int xs_is_hex(const char *str)
/* returns 1 if str is an hex string */
{
    while (*str) {
        if (strchr("0123456789abcdefABCDEF", *str++) == NULL)
            return 0;
    }

    return 1;
}


unsigned int xs_hash_func(const char *data, int size)
/* a general purpose hashing function */
{
    unsigned int hash = 0x666;
    int n;

    for (n = 0; n < size; n++) {
        hash ^= data[n];
        hash *= 111111111;
    }

    return hash ^ hash >> 16;
}


#endif /* XS_IMPLEMENTATION */

#endif /* _XS_H */
