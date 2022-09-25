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

#define _XS_BLK_SIZE 16
#define _xs_blk_size(sz) ((((sz) + _XS_BLK_SIZE) / _XS_BLK_SIZE) * _XS_BLK_SIZE)

void _xs_destroy(char **var);
#define xs_debug() kill(getpid(), 5)
xstype xs_type(const char *data);
int xs_size(const char *data);
int xs_is_null(char *data);
d_char *xs_dup(const char *data);
d_char *xs_expand(d_char *data, int offset, int size);
d_char *xs_collapse(d_char *data, int offset, int size);
d_char *xs_insert_m(d_char *data, int offset, const char *mem, int size);
#define xs_insert(data, offset, data2) xs_insert_m(data, offset, data2, xs_size(data2))
#define xs_append_m(data, mem, size) xs_insert_m(data, xs_size(data) - 1, mem, size)
d_char *xs_str_new(const char *str);
#define xs_str_cat(str1, str2) xs_insert(str1, xs_size(str1) - 1, str2)
d_char *xs_replace(const char *str, const char *sfrom, const char *sto);
d_char *xs_fmt(const char *fmt, ...);
int xs_str_in(char *haystack, char *needle);
int xs_startswith(char *str, char *prefix);
int xs_endswith(char *str, char *postfix);
d_char *xs_crop(d_char *str, int start, int end);
d_char *xs_strip(d_char *str);
d_char *xs_tolower(d_char *str);
d_char *xs_list_new(void);
d_char *xs_list_append_m(d_char *list, const char *mem, int dsz);
#define xs_list_append(list, data) xs_list_append_m(list, data, xs_size(data))
int xs_list_iter(char **list, char **value);
int xs_list_len(char *list);
char *xs_list_get(char *list, int num);
int xs_list_in(char *list, char *val);
d_char *xs_join(char *list, const char *sep);
d_char *xs_split_n(const char *str, const char *sep, int times);
#define xs_split(str, sep) xs_split_n(str, sep, 0xfffffff)
d_char *xs_dict_new(void);
d_char *xs_dict_append_m(d_char *dict, const char *key, const char *mem, int dsz);
#define xs_dict_append(dict, key, data) xs_dict_append_m(dict, key, data, xs_size(data))
int xs_dict_iter(char **dict, char **key, char **value);
char *xs_dict_get(char *dict, const char *key);
d_char *xs_dict_del(d_char *dict, const char *key);
d_char *xs_dict_set(d_char *dict, const char *key, const char *data);
d_char *xs_val_new(xstype t);
d_char *xs_number_new(float f);
float xs_number_get(char *v);

extern int _xs_debug;


#ifdef XS_IMPLEMENTATION

int _xs_debug = 0;

void _xs_destroy(char **var)
{
    if (_xs_debug)
        printf("_xs_destroy %p\n", var);

    free(*var);
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


int xs_size(const char *data)
/* returns the size of data in bytes */
{
    int len = 0;
    int c = 0;
    const char *p;

    if (data == NULL)
        return 0;

    switch (xs_type(data)) {
    case XSTYPE_STRING:
        len = strlen(data) + 1;
        break;

    case XSTYPE_LIST:
        /* look for a balanced EOL */
        do {
            c += data[len] == XSTYPE_LIST ? 1 : data[len] == XSTYPE_EOL ? -1 : 0;
            len++;
        } while (c);

        break;

    case XSTYPE_DICT:
        /* look for a balanced EOD */
        do {
            c += data[len] == XSTYPE_DICT ? 1 : data[len] == XSTYPE_EOD ? -1 : 0;
            len++;
        } while (c);

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
        len = sizeof(float) + 1;

        break;

    default:
        len = 1;
    }

    return len;
}


int xs_is_null(char *data)
/* checks for null */
{
    return !!(data == NULL || xs_type(data) == XSTYPE_NULL);
}


d_char *xs_dup(const char *data)
/* creates a duplicate of data */
{
    int sz = xs_size(data);
    d_char *s = malloc(_xs_blk_size(sz));

    memcpy(s, data, sz);

    return s;
}


d_char *xs_expand(d_char *data, int offset, int size)
/* opens a hole in data */
{
    int sz = xs_size(data);
    int n;

    /* open room */
    if (sz == 0 || _xs_blk_size(sz) != _xs_blk_size(sz + size))
        data = realloc(data, _xs_blk_size(sz + size));

    /* move up the rest of the data */
    for (n = sz + size - 1; n >= offset + size; n--)
        data[n] = data[n - size];

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

    return realloc(data, _xs_blk_size(sz));
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


d_char *xs_replace(const char *str, const char *sfrom, const char *sto)
/* replaces all occurrences of sfrom with sto in str */
{
    d_char *s;
    char *ss;
    int sfsz;

    /* cache the sizes */
    sfsz = strlen(sfrom);

    /* create the new string */
    s = xs_str_new(NULL);

    while ((ss = strstr(str, sfrom)) != NULL) {
        /* copy the first part */
        s = xs_append_m(s, str, ss - str);

        /* copy sto */
        s = xs_str_cat(s, sto);

        /* move forward */
        str = ss + sfsz;
    }

    /* copy the rest */
    s = xs_str_cat(s, str);

    return s;
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
        n = _xs_blk_size(n + 1);
        s = calloc(n, 1);

        va_start(ap, fmt);
        n = vsnprintf(s, n, fmt, ap);
        va_end(ap);
    }

    return s;
}


int xs_str_in(char *haystack, char *needle)
/* finds needle in haystack and returns the offset or -1 */
{
    char *s;
    int r = -1;

    if ((s = strstr(haystack, needle)) != NULL)
        r = s - haystack;

    return r;
}


int xs_startswith(char *str, char *prefix)
/* returns true if str starts with prefix */
{
    return !!(xs_str_in(str, prefix) == 0);
}


int xs_endswith(char *str, char *postfix)
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


d_char *xs_strip(d_char *str)
/* strips the string of blanks from the start and the end */
{
    int s, e;

    for (s = 0; isspace(str[s]); s++);
    for (e = strlen(str); e > 0 && isspace(str[e - 1]); e--);

    return xs_crop(str, s, e);
}


d_char *xs_tolower(d_char *str)
/* convert to lowercase */
{
    int n;

    for (n = 0; str[n]; n++)
        str[n] = tolower(str[n]);

    return str;
}


/** lists **/

d_char *xs_list_new(void)
/* creates a new list */
{
    d_char *list;

    list = malloc(_xs_blk_size(2));
    list[0] = XSTYPE_LIST;
    list[1] = XSTYPE_EOL;

    return list;
}


d_char *xs_list_append_m(d_char *list, const char *mem, int dsz)
/* adds a memory block to the list */
{
    char c = XSTYPE_LITEM;
    int lsz = xs_size(list);

    list = xs_insert_m(list, lsz - 1, &c, 1);
    list = xs_insert_m(list, lsz, mem, dsz);

    return list;
}


int xs_list_iter(char **list, char **value)
/* iterates a list value */
{
    int goon = 1;
    char *p;

    if (list == NULL || *list == NULL)
        return 0;

    p = *list;

    /* skip a possible start of the list */
    if (*p == XSTYPE_LIST)
        p++;

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
    char *v, *r = NULL;
    int c = 0;

    if (num < 0)
        num = xs_list_len(list) + num;

    while (xs_list_iter(&list, &v)) {
        if (c == num) {
            r = v;
            break;
        }
        c++;
    }

    return r;
}


int xs_list_in(char *list, char *val)
/* returns the position of val in list or -1 */
{
    int n = 0;
    int r = -1;
    char *v;
    int sz = xs_size(val);

    while (r == -1 && xs_list_iter(&list, &v)) {
        int vsz = xs_size(v);

        if (sz == vsz && memcmp(val, v, sz) == 0)
            r = n;

        n++;
    }

    return r;
}


d_char *xs_join(char *list, const char *sep)
/* joins a list into a string */
{
    d_char *s;
    char *v;
    int c = 0;

    s = xs_str_new(NULL);

    while (xs_list_iter(&list, &v)) {
        /* refuse to join non-string values */
        if (xs_type(v) == XSTYPE_STRING) {
            /* add the separator */
            if (c != 0)
                s = xs_str_cat(s, sep);

            /* add the element */
            s = xs_str_cat(s, v);

            c++;
        }
    }

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

    dict = malloc(_xs_blk_size(2));
    dict[0] = XSTYPE_DICT;
    dict[1] = XSTYPE_EOD;

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

    /* skip a possible start of the list */
    if (*p == XSTYPE_DICT)
        p++;

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
    char *k, *v, *r = NULL;

    while (xs_dict_iter(&dict, &k, &v)) {
        if (strcmp(k, key) == 0) {
            r = v;
            break;
        }
    }

    return r;
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
    d_char *v = malloc(_xs_blk_size(1));

    v[0] = t;

    return v;
}


d_char *xs_number_new(float f)
/* adds a new number value */
{
    d_char *v = malloc(_xs_blk_size(1 + sizeof(float)));

    v[0] = XSTYPE_NUMBER;
    memcpy(&v[1], &f, sizeof(float));

    return v;
}


float xs_number_get(char *v)
/* gets the number as a float */
{
    float f = 0.0;

    if (v[0] == XSTYPE_NUMBER)
        memcpy(&f, &v[1], sizeof(float));

    return f;
}


#endif /* XS_IMPLEMENTATION */

#endif /* _XS_H */
