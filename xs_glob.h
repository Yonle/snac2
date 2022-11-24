/* copyright (c) 2022 grunfink - MIT license */

#ifndef _XS_GLOB_H

#define _XS_GLOB_H

d_char *xs_glob_n(const char *spec, int basename, int reverse, int max);
#define xs_glob(spec, basename, reverse) xs_glob_n(spec, basename, reverse, XS_ALL)


#ifdef XS_IMPLEMENTATION

#include <glob.h>

d_char *xs_glob_n(const char *spec, int basename, int reverse, int max)
/* does a globbing and returns the found files */
{
    glob_t globbuf;
    d_char *list = xs_list_new();

    if (glob(spec, 0, NULL, &globbuf) == 0) {
        int n;

        if (max > globbuf.gl_pathc)
            max = globbuf.gl_pathc;

        for (n = 0; n < max; n++) {
            char *p;

            if (reverse)
                p = globbuf.gl_pathv[globbuf.gl_pathc - n - 1];
            else
                p = globbuf.gl_pathv[n];

            if (p != NULL) {
                if (basename) {
                    if ((p = strrchr(p, '/')) == NULL)
                        continue;

                    p++;
                }

                list = xs_list_append(list, p);
            }
        }
    }

    globfree(&globbuf);

    return list;
}


#endif /* XS_IMPLEMENTATION */

#endif /* _XS_GLOB_H */
