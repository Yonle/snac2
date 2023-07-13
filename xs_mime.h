/* copyright (c) 2022 - 2023 grunfink / MIT license */

#ifndef _XS_MIME

#define _XS_MIME

char *xs_mime_by_ext(const char *file);

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
    { "video/mp4",          ".mp4"},
    { "video/mp4",          ".mpg4"},
    { "video/mp4",          ".m4v"},
    { "video/webm",         ".webm"},
    { "video/quicktime",    ".mov"},
    { "video/3gpp",         ".3gp"},
    { "video/ogg",          ".ogv"},
    { "video/flv",          ".flv"},
    { "audio/mp3",          ".mp3"},
    { "audio/ogg",          ".ogg"},
    { "audio/ogg",          ".oga"},
    { "audio/ogg",          ".opus"},
    { "audio/flac",         ".flac"},
    { "audio/wav",          ".wav"},
    { "audio/wma",          ".wma"},
    { "audio/aac",          ".aac"},
    { "audio/aac",          ".m4a"},
    { "text/css",           ".css" },
    { "text/html",          ".html" },
    { "text/plain",         ".txt" },
    { "text/xml",           ".xml" },
    { "text/markdown",      ".md" },
    { "text/gemini",        ".gmi" },
    { NULL, NULL }
};


char *xs_mime_by_ext(const char *file)
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
