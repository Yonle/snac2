/* copyright (c) 2022 - 2023 grunfink / MIT license */

#ifndef _XS_RANDOM_H

#define _XS_RANDOM_H

unsigned int xs_rnd_int32_d(unsigned int *seed);
void *xs_rnd_buf(void *buf, int size);

#ifdef XS_IMPLEMENTATION

#include <stdio.h>
#include <sys/time.h>
#include <unistd.h>
#include <stdlib.h>

unsigned int xs_rnd_int32_d(unsigned int *seed)
/* returns a deterministic random integer. If seed is NULL, uses a static one */
{
    static unsigned int s = 0;

    if (seed == NULL)
        seed = &s;

    if (*seed == 0) {
        struct timeval tv;

        gettimeofday(&tv, NULL);
        *seed = tv.tv_sec ^ tv.tv_usec ^ getpid();
    }

    /* Linear congruential generator by Numerical Recipes */
    *seed = (*seed * 1664525) + 1013904223;

    return *seed;
}


void *xs_rnd_buf(void *buf, int size)
/* fills buf with random data */
{
#ifdef __OpenBSD__

    /* available since OpenBSD 2.2 */
    arc4random_buf(buf, size);

#else

    FILE *f;
    int done = 0;

    if ((f = fopen("/dev/urandom", "r")) != NULL) {
        /* fill with great random data from the system */
        if (fread(buf, size, 1, f) == 1)
            done = 1;

        fclose(f);
    }

    if (!done) {
        /* fill the buffer with poor quality, deterministic data */
        unsigned int s   = 0;
        unsigned char *p = (unsigned char *)buf;
        int n            = size / sizeof(s);

        /* fill with full integers */
        while (n--) {
            xs_rnd_int32_d(&s);
            p = memcpy(p, &s, sizeof(s)) + sizeof(s);
        }

        if ((n = size % sizeof(s))) {
            /* fill the remaining */
            xs_rnd_int32_d(&s);
            memcpy(p, &s, n);
        }
    }

#endif /* __OpenBSD__ */

    return buf;
}


#endif /* XS_IMPLEMENTATION */

#endif /* XS_RANDOM_H */
