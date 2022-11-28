/* copyright (c) 2022 grunfink - MIT license */

#ifndef _XS_SET_H

#define _XS_SET_H

typedef struct _xs_set {
    int elems;              /* number of hash entries */
    int used;               /* number of used hash entries */
    int *hash;              /* hashed offsets */
    d_char *list;           /* list of stored data */
} xs_set;

void xs_set_init(xs_set *s);
d_char *xs_set_result(xs_set *s);
void xs_set_free(xs_set *s);
int xs_set_add(xs_set *s, const char *data);


#ifdef XS_IMPLEMENTATION


void xs_set_init(xs_set *s)
/* initializes a set */
{
    /* arbitrary default */
    s->elems = 256;
    s->used  = 0;
    s->hash  = xs_realloc(NULL, s->elems * sizeof(int));
    s->list  = xs_list_new();

    memset(s->hash, '\0', s->elems * sizeof(int));
}


d_char *xs_set_result(xs_set *s)
/* returns the set as a list and frees it */
{
    d_char *list = s->list;
    s->list = NULL;
    s->hash = xs_free(s->hash);

    return list;
}


void xs_set_free(xs_set *s)
/* frees a set, dropping the list */
{
    free(xs_set_result(s));
}


static unsigned int _calc_hash(const char *data, int size)
{
    unsigned int hash = 0x666;
    int n;

    for (n = 0; n < size; n++) {
        hash ^= data[n];
        hash *= 111111111;
    }

    return hash ^ hash >> 16;
}


static int _store_hash(xs_set *s, const char *data, int value)
{
    unsigned int hash, i;
    int sz = xs_size(data);

    hash = _calc_hash(data, sz);

    while (s->hash[(i = hash % s->elems)]) {
        /* get the pointer to the stored data */
        char *p = &s->list[s->hash[i]];

        /* already here? */
        if (memcmp(p, data, sz) == 0)
            return 0;

        /* try next value */
        hash++;
    }

    /* store the new value */
    s->hash[i] = value;

    s->used++;

    return 1;
}


int xs_set_add(xs_set *s, const char *data)
/* adds the data to the set */
/* returns: 1 if added, 0 if already there */
{
    /* is it 'full'? */
    if (s->used >= s->elems / 2) {
        char *p, *v;

        /* expand! */
        s->elems *= 2;
        s->used  = 0;
        s->hash  = xs_realloc(s->hash, s->elems * sizeof(int));

        memset(s->hash, '\0', s->elems * sizeof(int));

        /* add the list elements back */
        p = s->list;
        while (xs_list_iter(&p, &v))
            _store_hash(s, v, v - s->list);
    }

    int ret = _store_hash(s, data, xs_size(s->list));

    /* if it's new, add the data */
    if (ret)
        s->list = xs_list_append_m(s->list, data, xs_size(data));

    return ret;
}

#endif /* XS_IMPLEMENTATION */

#endif /* XS_SET_H */