/* snac - A simple, minimalistic ActivityPub instance */
/* copyright (c) 2022 - 2023 grunfink / MIT license */

#include "xs.h"
#include "xs_io.h"
#include "xs_encdec.h"
#include "xs_json.h"
#include "xs_socket.h"
#include "xs_httpd.h"
#include "xs_mime.h"

#include "snac.h"

#include <setjmp.h>
#include <pthread.h>
#include <semaphore.h>

#include <sys/resource.h> // for getrlimit()

#ifdef USE_POLL_FOR_SLEEP
#include <poll.h>
#endif


/* nodeinfo 2.0 template */
const char *nodeinfo_2_0_template = ""
    "{\"version\":\"2.0\","
    "\"software\":{\"name\":\"snac\",\"version\":\"" VERSION "\"},"
    "\"protocols\":[\"activitypub\"],"
    "\"services\":{\"outbound\":[],\"inbound\":[]},"
    "\"usage\":{\"users\":{\"total\":%d,\"activeMonth\":%d,\"activeHalfyear\":%d},"
    "\"localPosts\":%d},"
    "\"openRegistrations\":false,\"metadata\":{}}";

d_char *nodeinfo_2_0(void)
/* builds a nodeinfo json object */
{
    xs *users   = user_list();
    int n_users = xs_list_len(users);
    int n_posts = 0; /* to be implemented someday */

    return xs_fmt(nodeinfo_2_0_template, n_users, n_users, n_users, n_posts);
}


int server_get_handler(d_char *req, char *q_path,
                       char **body, int *b_size, char **ctype)
/* basic server services */
{
    int status = 0;

    /* is it the server root? */
    if (*q_path == '\0') {
        /* try to open greeting.html */
        xs *fn = xs_fmt("%s/greeting.html", srv_basedir);
        FILE *f;

        if ((f = fopen(fn, "r")) != NULL) {
            d_char *s = xs_readall(f);
            fclose(f);

            status = 200;

            /* replace %host% */
            s = xs_replace_i(s, "%host%", xs_dict_get(srv_config, "host"));

            /* does it have a %userlist% mark? */
            if (xs_str_in(s, "%userlist%") != -1) {
                char *host = xs_dict_get(srv_config, "host");
                xs *list = user_list();
                char *p, *uid;
                xs *ul = xs_str_new("<ul class=\"snac-user-list\">\n");

                p = list;
                while (xs_list_iter(&p, &uid)) {
                    snac snac;

                    if (user_open(&snac, uid)) {
                        xs *u = xs_fmt(
                            "<li><a href=\"%s\">@%s@%s (%s)</a></li>\n",
                                snac.actor, uid, host,
                                xs_dict_get(snac.config, "name"));

                        ul = xs_str_cat(ul, u);

                        user_free(&snac);
                    }
                }

                ul = xs_str_cat(ul, "</ul>\n");

                s = xs_replace_i(s, "%userlist%", ul);
            }

            *body = s;
        }
    }
    else
    if (strcmp(q_path, "/susie.png") == 0 || strcmp(q_path, "/favicon.ico") == 0 ) {
        status = 200;
        *body  = xs_base64_dec(default_avatar_base64(), b_size);
        *ctype = "image/png";
    }
    else
    if (strcmp(q_path, "/.well-known/nodeinfo") == 0) {
        status = 200;
        *ctype = "application/json; charset=utf-8";
        *body  = xs_fmt("{\"links\":["
            "{\"rel\":\"http:/" "/nodeinfo.diaspora.software/ns/schema/2.0\","
            "\"href\":\"%s/nodeinfo_2_0\"}]}",
            srv_baseurl);
    }
    else
    if (strcmp(q_path, "/nodeinfo_2_0") == 0) {
        status = 200;
        *ctype = "application/json; charset=utf-8";
        *body  = nodeinfo_2_0();
    }
    else
    if (strcmp(q_path, "/robots.txt") == 0) {
        status = 200;
        *ctype = "text/plain";
        *body  = xs_str_new("User-agent: *\n"
                            "Disallow: /\n");
    }

    if (status != 0)
        srv_debug(1, xs_fmt("server_get_handler serving '%s' %d", q_path, status));

    return status;
}


void httpd_connection(FILE *f)
/* the connection processor */
{
    xs *req;
    char *method;
    int status   = 0;
    d_char *body = NULL;
    int b_size   = 0;
    char *ctype  = NULL;
    xs *headers  = NULL;
    xs *q_path   = NULL;
    xs *payload  = NULL;
    int p_size   = 0;
    char *p;

    req = xs_httpd_request(f, &payload, &p_size);

    if (req == NULL) {
        /* probably because a timeout */
        fclose(f);
        return;
    }

    method = xs_dict_get(req, "method");
    q_path = xs_dup(xs_dict_get(req, "path"));

    /* crop the q_path from leading / and the prefix */
    if (xs_endswith(q_path, "/"))
        q_path = xs_crop_i(q_path, 0, -1);

    p = xs_dict_get(srv_config, "prefix");
    if (xs_startswith(q_path, p))
        q_path = xs_crop_i(q_path, strlen(p), 0);

    if (strcmp(method, "GET") == 0 || strcmp(method, "HEAD") == 0) {
        /* cascade through */
        if (status == 0)
            status = server_get_handler(req, q_path, &body, &b_size, &ctype);

        if (status == 0)
            status = webfinger_get_handler(req, q_path, &body, &b_size, &ctype);

        if (status == 0)
            status = activitypub_get_handler(req, q_path, &body, &b_size, &ctype);

        if (status == 0)
            status = html_get_handler(req, q_path, &body, &b_size, &ctype);
    }
    else
    if (strcmp(method, "POST") == 0) {
        if (status == 0)
            status = activitypub_post_handler(req, q_path,
                        payload, p_size, &body, &b_size, &ctype);

        if (status == 0)
            status = html_post_handler(req, q_path,
                        payload, p_size, &body, &b_size, &ctype);
    }

    /* let's go */
    headers = xs_dict_new();

    /* unattended? it's an error */
    if (status == 0) {
        srv_debug(1, xs_fmt("httpd_connection unattended %s %s", method, q_path));
        status = 404;
    }

    if (status == 404)
        body = xs_str_new("<h1>404 Not Found</h1>");

    if (status == 400 && body != NULL)
        body = xs_str_new("<h1>400 Bad Request</h1>");

    if (status == 303)
        headers = xs_dict_append(headers, "location", body);

    if (status == 401)
        headers = xs_dict_append(headers, "WWW-Authenticate", "Basic realm=\"IDENTIFY\"");

    if (ctype == NULL)
        ctype = "text/html; charset=utf-8";

    headers = xs_dict_append(headers, "content-type", ctype);
    headers = xs_dict_append(headers, "x-creator",    USER_AGENT);

    if (b_size == 0 && body != NULL)
        b_size = strlen(body);

    /* if it was a HEAD, no body will be sent */
    if (strcmp(method, "HEAD") == 0)
        body = xs_free(body);

    xs_httpd_response(f, status, headers, body, b_size);

    fclose(f);

    srv_archive("RECV", req, payload, p_size, status, headers, body, b_size);

    xs_free(body);
}


static jmp_buf on_break;


void term_handler(int s)
{
    longjmp(on_break, 1);
}


/** job control **/

/* mutex to access the lists of jobs */
static pthread_mutex_t job_mutex;

/* semaphre to trigger job processing */
static sem_t job_sem;

/* fifo of jobs */
xs_list *job_fifo = NULL;


int job_fifo_ready(void)
/* returns true if the job fifo is ready */
{
    return job_fifo != NULL;
}


void job_post(const xs_val *job, int urgent)
/* posts a job for the threads to process it */
{
    if (job != NULL) {
        /* lock the mutex */
        pthread_mutex_lock(&job_mutex);

        /* add to the fifo */
        if (job_fifo != NULL) {
            if (urgent)
                job_fifo = xs_list_insert(job_fifo, 0, job);
            else
                job_fifo = xs_list_append(job_fifo, job);
        }

        /* unlock the mutex */
        pthread_mutex_unlock(&job_mutex);
    }

    /* ask for someone to attend it */
    sem_post(&job_sem);
}


void job_wait(xs_val **job)
/* waits for an available job */
{
    *job = NULL;

    if (sem_wait(&job_sem) == 0) {
        /* lock the mutex */
        pthread_mutex_lock(&job_mutex);

        /* dequeue */
        if (job_fifo != NULL)
            job_fifo = xs_list_shift(job_fifo, job);

        /* unlock the mutex */
        pthread_mutex_unlock(&job_mutex);
    }
}


#ifndef MAX_THREADS
#define MAX_THREADS 256
#endif

static void *job_thread(void *arg)
/* job thread */
{
    int pid = (char *) arg - (char *) 0x0;

    srv_debug(1, xs_fmt("job thread %d started", pid));

    for (;;) {
        xs *job = NULL;

        job_wait(&job);

        srv_debug(1, xs_fmt("job thread %d wake up", pid));

        if (job == NULL)
            break;

        if (xs_type(job) == XSTYPE_DATA) {
            /* it's a socket */
            FILE *f = NULL;

            xs_data_get(job, &f);

            if (f != NULL)
                httpd_connection(f);
        }
        else {
            /* it's a q_item */
            process_queue_item(job);
        }
    }

    srv_debug(1, xs_fmt("job thread %d stopped", pid));

    return NULL;
}

/* background thread sleep control */
static pthread_mutex_t sleep_mutex;
static pthread_cond_t  sleep_cond;

static void *background_thread(void *arg)
/* background thread (queue management and other things) */
{
    time_t purge_time;

    /* first purge time */
    purge_time = time(NULL) + 10 * 60;

    srv_log(xs_fmt("background thread started"));

    while (srv_running) {
        time_t t;
        int cnt = 0;

        {
            xs *list = user_list();
            char *p, *uid;

            /* process queues for all users */
            p = list;
            while (xs_list_iter(&p, &uid)) {
                snac snac;

                if (user_open(&snac, uid)) {
                    cnt += process_user_queue(&snac);
                    user_free(&snac);
                }
            }
        }

        /* global queue */
        cnt += process_queue();

        /* time to purge? */
        if ((t = time(NULL)) > purge_time) {
            /* next purge time is tomorrow */
            purge_time = t + 24 * 60 * 60;

            xs *q_item = xs_dict_new();
            q_item = xs_dict_append(q_item, "type", "purge");
            job_post(q_item, 0);
        }

        if (cnt == 0) {
            /* sleep 3 seconds */

#ifdef USE_POLL_FOR_SLEEP
            poll(NULL, 0, 3 * 1000);
#else
            struct timespec ts;

            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec += 3;

            pthread_mutex_lock(&sleep_mutex);
            while (pthread_cond_timedwait(&sleep_cond, &sleep_mutex, &ts) == 0);
            pthread_mutex_unlock(&sleep_mutex);
#endif
        }
    }

    srv_log(xs_fmt("background thread stopped"));

    return NULL;
}


void httpd(void)
/* starts the server */
{
    char *address;
    int port;
    int rs;
    pthread_t threads[MAX_THREADS] = {0};
    int n_threads = 0;
    int n;

    address = xs_dict_get(srv_config, "address");
    port    = xs_number_get(xs_dict_get(srv_config, "port"));

    if ((rs = xs_socket_server(address, port)) == -1) {
        srv_log(xs_fmt("cannot bind socket to %s:%d", address, port));
        return;
    }

    srv_running = 1;

    signal(SIGPIPE, SIG_IGN);
    signal(SIGTERM, term_handler);
    signal(SIGINT,  term_handler);

    srv_log(xs_fmt("httpd start %s:%d %s", address, port, USER_AGENT));

    /* show the number of usable file descriptors */
    struct rlimit r;
    getrlimit(RLIMIT_NOFILE, &r);
    srv_debug(0, xs_fmt("available (rlimit) fds: %d (cur) / %d (max)",
                        (int) r.rlim_cur, (int) r.rlim_max));

    /* initialize the job control engine */
    pthread_mutex_init(&job_mutex, NULL);
    sem_init(&job_sem, 0, 0);
    job_fifo = xs_list_new();

    /* initialize sleep control */
    pthread_mutex_init(&sleep_mutex, NULL);
    pthread_cond_init(&sleep_cond, NULL);

    n_threads = xs_number_get(xs_dict_get(srv_config, "num_threads"));

#ifdef _SC_NPROCESSORS_ONLN
    if (n_threads == 0) {
        /* get number of CPUs on the machine */
        n_threads = sysconf(_SC_NPROCESSORS_ONLN);
    }
#endif

    if (n_threads < 4)
        n_threads = 4;

    if (n_threads > MAX_THREADS)
        n_threads = MAX_THREADS;

    srv_debug(0, xs_fmt("using %d threads", n_threads));

    /* thread #0 is the background thread */
    pthread_create(&threads[0], NULL, background_thread, NULL);

    /* the rest of threads are for job processing */
    char *ptr = (char *) 0x1;
    for (n = 1; n < n_threads; n++)
        pthread_create(&threads[n], NULL, job_thread, ptr++);

    if (setjmp(on_break) == 0) {
        for (;;) {
            FILE *f = xs_socket_accept(rs);

            if (f != NULL) {
                xs *job = xs_data_new(&f, sizeof(FILE *));
                job_post(job, 1);
            }
            else
                break;
        }
    }

    srv_running = 0;

    /* send as many empty jobs as working threads */
    for (n = 1; n < n_threads; n++)
        job_post(NULL, 0);

    /* wait for all the threads to exit */
    for (n = 0; n < n_threads; n++)
        pthread_join(threads[n], NULL);

    pthread_mutex_lock(&job_mutex);
    job_fifo = xs_free(job_fifo);
    pthread_mutex_unlock(&job_mutex);

    srv_log(xs_fmt("httpd stop %s:%d", address, port));
}
