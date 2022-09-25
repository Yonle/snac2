/* copyright (c) 2022 grunfink - MIT license */

#ifndef _XS_MIME

#define _XS_MIME

char *xs_mime_by_ext(char *file);

#ifdef XS_IMPLEMENTATION

/* intentionally brain-dead simple */
struct _mime_info {
    char *type;
    char *ext;
} mime_info[] = {
    { "application/json",   ".json" },
    { "image/gif",          ".gif" },
    { "image/jpeg",         ".jpeg" },
    { "image/jpeg",         ".jpg" },
    { "image/png",          ".png" },
    { "image/webp",         ".webp" },
    { "text/css",           ".css" },
    { "text/html",          ".html" },
    { "text/plain",         ".txt" },
    { "text/xml",           ".xml" },
    { NULL, NULL }
};


char *xs_mime_by_ext(char *file)
/* returns the MIME type by file extension */
{
    struct _mime_info *mi = mime_info;
    char *p = NULL;

    while (p == NULL && mi->type != NULL) {
        if (xs_endswith(file, mi->ext))
            p = mi->type;

        mi++;
    }

    if (p == NULL)
        p = "application/octet-stream";

    return p;
}


#endif /* XS_IMPLEMENTATION */

#endif /* XS_MIME */
