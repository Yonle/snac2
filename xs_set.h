/* copyright (c) 2022 grunfink - MIT license */

#ifndef _XS_SET_H

#define _XS_SET_H

typedef struct _xs_set {
    int elems;              /* number of hash entries */
    int used;               /* number of used hash entries */
    d_char *list;           /* list of stored data */
    int hash[];             /* hashed offsets */
} xs_set;

xs_set *xs_set_new(int elems);
void xs_set_free(xs_set *s);
int xs_set_add(xs_set *s, const char *data);


#ifdef XS_IMPLEMENTATION

xs_set *xs_set_new(int elems)
/* creates a new set with a maximum of size hashed data */
{
    int sz = sizeof(struct _xs_set) + sizeof(int) * elems;
    xs_set *s = xs_realloc(NULL, sz);

    memset(s, '\0', sz);

    /* initialize */
    s->elems  = elems;
    s->list   = xs_list_new();

    return s;
}


void xs_set_free(xs_set *s)
/* frees a set */
{
    xs_free(s->list);
    xs_free(s);
}


unsigned int _xs_set_hash(const char *data, int size)
{
    unsigned int hash = 0x666;
    int n;

    for (n = 0; n < size; n++) {
        hash ^= data[n];
        hash *= 111111111;
    }

    return hash ^ hash >> 16;
}


int xs_set_add(xs_set *s, const char *data)
/* adds the data to the set */
/* returns: 1 if added, 0 if already there, -1 if it's full */
{
    unsigned int hash, i;
    int sz = xs_size(data);

    hash = _xs_set_hash(data, sz);

    while (s->hash[(i = hash % s->elems)]) {
        /* get the pointer to the stored data */
        char *p = &s->list[s->hash[i]];

        /* already here? */
        if (memcmp(p, data, sz) == 0)
            return 0;

        /* try next value */
        hash++;
    }

    /* is it full? fail */
    if (s->used == s->elems / 2)
        return -1;

    /* store the position */
    s->hash[i] = xs_size(s->list);

    /* add the data */
    s->list = xs_list_append_m(s->list, data, sz);

    s->used++;

    return 1;
}

#endif /* XS_IMPLEMENTATION */

#endif /* XS_SET_H */