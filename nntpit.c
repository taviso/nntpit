/* 
 * This file is part of nntpit, https://github.com/taviso/nntpit.
 *
 * Based on nntpsink, Copyright (c) 2011-2014 Felicity Tarnell.
 *
 */

#include  <stdlib.h>
#include  <limits.h>
#include  <sys/types.h>
#include  <sys/socket.h>
#include  <sys/resource.h>

#include  <netinet/in.h>
#include  <netinet/tcp.h>

#include  <stdlib.h>
#include  <stdio.h>
#include  <unistd.h>
#include  <string.h>
#include  <netdb.h>
#include  <errno.h>
#include  <fcntl.h>
#include  <ctype.h>
#include  <assert.h>
#include  <time.h>
#include  <stdarg.h>
#include  <pthread.h>
#include <stdbool.h>

#include  <ev.h>

#include <json.h>
#include <glib.h>

#include  "nntpit.h"
#include  "charq.h"

#include "json_object.h"
#include "jsonutil.h"
#include "reddit.h"

static json_object *newsrc;
static json_object *spool;
static json_object *groupset;

char  *listen_host;
char  *port;
int  debug;

int  do_ihave = 1;
int  do_streaming = 1;

#define   ignore_errno(e) ((e) == EAGAIN || (e) == EINPROGRESS || (e) == EWOULDBLOCK)

typedef struct thread {
  pthread_t    th_id;
  struct ev_loop    *th_loop;
  pthread_mutex_t    th_mtx;
  struct ev_prepare  th_deadlist_ev;
  struct client   *th_deadlist;

  int     *th_accept;
  int      th_naccept;
  int      th_acceptsize;
  ev_async     th_wakeup;

  int      th_nsend,
         th_naccepted,
         th_nrefuse,
         th_ndefer,
         th_nreject;
  ev_timer     th_stats;
} thread_t;

thread_t *threads;
int   nthreads = 1;
int   next_thread;

void   thread_wakeup(struct ev_loop *, ev_async *, int);
void  *thread_run(void *);
void   thread_accept(thread_t *);
void   thread_deadlist(struct ev_loop *, ev_prepare *w, int revents);
void   do_thread_stats(struct ev_loop *, ev_timer *w, int);

typedef enum client_state {
  CL_NORMAL,
  CL_TAKETHIS,
  CL_IHAVE
} client_state_t;

#define CL_DEAD   0x1

typedef struct client {
  thread_t  *cl_thread;
  int    cl_fd;
  ev_io    cl_readable;
  ev_io    cl_writable;
  charq_t   *cl_wrbuf;
  charq_t   *cl_rdbuf;
  client_state_t   cl_state;
  int    cl_flags;
  char    *cl_msgid;
  struct client *cl_next;
} client_t;

void  client_read(struct ev_loop *, ev_io *, int);
void  client_write(struct ev_loop *, ev_io *, int);
void  client_flush(client_t *);
void  client_close(client_t *);
void  client_send(client_t *, char const *);
void  client_printf(client_t *, char const *, ...);
void  client_vprintf(client_t *, char const *, va_list);

typedef struct listener {
  int ln_fd;
  ev_io ln_readable;
} listener_t;

void  listener_accept(struct ev_loop *, ev_io *, int);

struct ev_loop  *main_loop;
ev_timer   stats_timer;
time_t     start_time;

void   usage(char const *);

int nsend, naccept, ndefer, nreject, nrefuse;
void  do_stats(struct ev_loop *, ev_timer *w, int);
pthread_mutex_t stats_mtx;

void
usage(p)
  char const  *p;
{
  fprintf(stderr,
"usage: %s [-VDhIS] [-t <threads>] [-l <host>] [-p <port>]\n"
"\n"
"    -V                   print version and exit\n"
"    -h                   print this text\n"
"    -D                   show data sent/received\n"
"    -I                   support IHAVE only (not streaming)\n"
"    -S                   support streaming only (not IHAVE)\n"
"    -l <host>            address to listen on (default: localhost)\n"
"    -p <port>            port to listen on (default: 119)\n"
"    -t <threads>         number of processing threads (default: 1)\n"
, p);
}

int main(int argc, char **argv)
{
    int  c, i;
    char  *progname = argv[0];
    struct addrinfo *res, *r, hints;


    newsrc = json_object_from_file("newsrc");
    spool = json_object_from_file("spool");

    // Use an empty spool if that didn't work.
    spool = spool ? spool : json_object_new_object();

    // Use an empty newsrc if that didn't work.
    newsrc = newsrc ? newsrc : json_object_new_object();

    while ((c = getopt(argc, argv, "VDSIhl:p:t:")) != -1) {
        switch (c) {
            case 'V':
                printf("nntpit %s\n", PACKAGE_VERSION);
                return 0;

            case 'D':
                debug++;
                break;

            case 'I':
                do_streaming = 0;
                break;

            case 'S':
                do_ihave = 0;
                break;

            case 'l':
                free(listen_host);
                listen_host = strdup(optarg);
                break;

            case 'p':
                free(port);
                port = strdup(optarg);
                break;

            case 't':
                if ((nthreads = atoi(optarg)) <= 0) {
                    fprintf(stderr, "%s: threads must be greater than zero\n",
                            argv[0]);
                    return 1;
                }
                break;

            case 'h':
                usage(argv[0]);
                return 0;

            default:
                usage(argv[0]);
                return 1;
        }
    }
    argc -= optind;
    argv += optind;

    signal(SIGPIPE, SIG_IGN);

    if (!do_ihave && !do_streaming) {
        fprintf(stderr, "%s: -I and -S may not both be specified\n", progname);
        return 1;
    }

    if (!listen_host)
        listen_host = strdup("localhost");

    if (!port)
        port = strdup("119");

    if (argv[0]) {
        usage(progname);
        return 1;
    }

    main_loop = ev_loop_new(ev_supported_backends());

    bzero(&hints, sizeof(hints));
    hints.ai_family = PF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    i = getaddrinfo(listen_host, port, &hints, &res);

    if (i) {
        fprintf(stderr, "%s: %s:%s: %s\n",
                progname, listen_host, port, gai_strerror(i));
        return 1;
    }

    for (r = res; r; r = r->ai_next) {
        listener_t  *lsn = xcalloc(1, sizeof(*lsn));
        int    fl, one = 1;
        char     sname[NI_MAXHOST];

        if ((lsn->ln_fd = socket(r->ai_family, r->ai_socktype, r->ai_protocol)) == -1) {
            fprintf(stderr, "%s:%s: socket: %s\n",
                    listen_host, port, strerror(errno));
            return 1;
        }

        if ((fl = fcntl(lsn->ln_fd, F_GETFL, 0)) == -1) {
            fprintf(stderr, "%s:%s: fgetfl: %s\n",
                    listen_host, port, strerror(errno));
            return 1;
        }

        if (fcntl(lsn->ln_fd, F_SETFL, fl | O_NONBLOCK) == -1) {
            fprintf(stderr, "%s:%s: fsetfl: %s\n",
                    listen_host, port, strerror(errno));
            return 1;
        }

        if (setsockopt(lsn->ln_fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one)) == -1) {
            fprintf(stderr, "%s:%s: setsockopt(TCP_NODELAY): %s\n",
                    listen_host, port, strerror(errno));
            return 1;
        }

        if (setsockopt(lsn->ln_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) == -1) {
            fprintf(stderr, "%s:%s: setsockopt(SO_REUSEADDR): %s\n",
                    listen_host, port, strerror(errno));
            return 1;
        }

        if (bind(lsn->ln_fd, r->ai_addr, r->ai_addrlen) == -1) {
            getnameinfo(r->ai_addr, r->ai_addrlen, sname, sizeof(sname),
                    NULL, 0, NI_NUMERICHOST);
            fprintf(stderr, "%s[%s]:%s: bind: %s\n",
                    listen_host, sname, port, strerror(errno));
            return 1;
        }

        if (listen(lsn->ln_fd, 128) == -1) {
            fprintf(stderr, "%s:%s: listen: %s\n",
                    listen_host, port, strerror(errno));
            return 1;
        }

        ev_io_init(&lsn->ln_readable, listener_accept, lsn->ln_fd, EV_READ);
        lsn->ln_readable.data = lsn;

        ev_io_start(main_loop, &lsn->ln_readable);
    }

    freeaddrinfo(res);

    //ev_timer_init(&stats_timer, do_stats, 60., 60.);
    //ev_timer_start(main_loop, &stats_timer);

    threads = xcalloc(nthreads, sizeof(thread_t));
    for (i = 0; i < nthreads; i++) {
        thread_t  *th = &threads[i];

        th->th_loop = ev_loop_new(ev_supported_backends());

        ev_async_init(&th->th_wakeup, thread_wakeup);
        th->th_wakeup.data = th;

        ev_prepare_init(&th->th_deadlist_ev, thread_deadlist);
        th->th_deadlist_ev.data = th;

        ev_timer_init(&th->th_stats, do_thread_stats, .1, .1); 
        th->th_stats.data = th;

        pthread_mutex_init(&th->th_mtx, NULL);
        pthread_create(&th->th_id, NULL, thread_run, th);
    }

    time(&start_time);
    ev_run(main_loop, 0);

    reddit_spool_expunge(spool);
    json_object_to_file("newsrc", newsrc);
    json_object_to_file("spool", spool);
    json_object_put(newsrc);
    json_object_put(spool);
    return 0;
}

void *
thread_run(p)
  void  *p;
{
thread_t  *th = p;
  ev_async_start(th->th_loop, &th->th_wakeup);
  ev_prepare_start(th->th_loop, &th->th_deadlist_ev);
  ev_timer_start(th->th_loop, &th->th_stats);
  ev_run(th->th_loop, 0);
  return NULL;
}

void thread_wakeup(struct ev_loop *loop, ev_async *w, int revents) {
    thread_t *th = w->data;
    thread_accept(th);
}

void
thread_accept(th)
  thread_t  *th;
{
int i;

  pthread_mutex_lock(&th->th_mtx);
  
  for (i = 0; i < th->th_naccept; i++) {
  client_t  *client = xcalloc(1, sizeof(*client));
  int    one = 1;
  int    fd = th->th_accept[i];

    client->cl_fd = fd;
    if (setsockopt(client->cl_fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one)) == -1) {
      close(fd);
      free(client);
      continue;
    }

    client->cl_thread = th;
    client->cl_rdbuf = cq_new();
    client->cl_wrbuf = cq_new();

    ev_io_init(&client->cl_readable, client_read, client->cl_fd, EV_READ);
    client->cl_readable.data = client;

    ev_io_init(&client->cl_writable, client_write, client->cl_fd, EV_WRITE);
    client->cl_writable.data = client;

    ev_io_start(th->th_loop, &client->cl_readable);
    client_printf(client, "200 nntpit ready.\r\n");
    client_flush(client);
  }

  th->th_naccept = 0;
  pthread_mutex_unlock(&th->th_mtx);
}

void listener_accept(struct ev_loop *loop, ev_io *w, int revents)
{
    int fd;
    listener_t *lsn = w->data;
    struct sockaddr_storage addr;
    socklen_t addrlen;

    while ((fd = accept(lsn->ln_fd, (struct sockaddr *) &addr, &addrlen)) >= 0) {
        thread_t  *th = &threads[next_thread];

        pthread_mutex_lock(&th->th_mtx);

        if (++th->th_naccept > th->th_acceptsize) {
          th->th_accept = realloc(th->th_accept, sizeof(int) * (th->th_naccept * 2));
          th->th_acceptsize = th->th_naccept * 2;
        }

        th->th_accept[th->th_naccept - 1] = fd;

        ev_async_send(th->th_loop, &th->th_wakeup);
        pthread_mutex_unlock(&th->th_mtx);

        if (next_thread == (nthreads - 1))
          next_thread = 0;
        else
          ++next_thread;
      }

      if (!ignore_errno(errno))
        fprintf(stderr, "accept: %s", strerror(errno));
}

void client_write(struct ev_loop *loop, ev_io *w, int revents)
{
    client_t *cl = w->data;
    client_flush(cl);
}

void
client_destroy(cl)
  client_t  *cl;
{
  close(cl->cl_fd);
  cq_free(cl->cl_rdbuf);
  cq_free(cl->cl_wrbuf);
  free(cl->cl_msgid);
  free(cl);
}

void
client_flush(cl)
  client_t  *cl;
{
thread_t  *th = cl->cl_thread;
struct ev_loop  *loop = th->th_loop;

  if (cl->cl_flags & CL_DEAD)
    return;

  if (cq_write(cl->cl_wrbuf, cl->cl_fd) < 0) {
    if (ignore_errno(errno)) {
      ev_io_start(loop, &cl->cl_writable);
      return;
    }

    printf("[%d] write error: %s\n",
      cl->cl_fd, strerror(errno));
    client_close(cl);
    return;
  }

  ev_io_stop(loop, &cl->cl_writable);
}

void
client_close(cl)
  client_t  *cl;
{
thread_t  *th = cl->cl_thread;
struct ev_loop  *loop = th->th_loop;

  if (cl->cl_flags & CL_DEAD)
    return;

  ev_io_stop(loop, &cl->cl_writable);
  ev_io_stop(loop, &cl->cl_readable);
  cl->cl_flags |= CL_DEAD;

  cl->cl_next = th->th_deadlist;
  th->th_deadlist = cl;
}

void
client_send(cl, s)
  client_t  *cl;
  char const  *s;
{
  cq_append(cl->cl_wrbuf, s, strlen(s));
  if (cq_len(cl->cl_wrbuf) > 1024)
    client_flush(cl);
}

void
client_vprintf(client_t *cl, char const *fmt, va_list ap)
{
char  line[1024];
int n;
  n = vsnprintf(line, sizeof(line), fmt, ap);
  cq_append(cl->cl_wrbuf, line, n);

  //if (cq_len(cl->cl_wrbuf) > 1024)
    client_flush(cl);
}

void
client_printf(client_t *cl, char const *fmt, ...)
{
va_list ap;
  va_start(ap, fmt);
  client_vprintf(cl, fmt, ap);
  va_end(ap);
}

void handle_list_cmd(client_t *cl, const char *param)
{
    // List available newsgroups
    if (!param || strcasecmp(param, "ACTIVE") == 0) {
        client_printf(cl, "215 subreddits available\r\n");

        // The syntax is documented here: https://tools.ietf.org/html/rfc977#section-3.6.1
        json_object_object_foreach(newsrc, group, artmap) {
            client_printf(cl, "%s %d %d n\r\n",
                              group,
                              reddit_spool_highwatermark(artmap),
                              reddit_spool_lowwatermark(artmap));
        }

        client_printf(cl, ".\r\n");
        return;
    } else if (strcasecmp(param, "OVERVIEW.FMT") == 0) {
        client_printf(cl, "215 information follows\r\n");
        client_printf(cl, "subject\r\n");
        client_printf(cl, "from\r\n");
        client_printf(cl, "date\r\n");
        client_printf(cl, "message-id\r\n");
        client_printf(cl, "references\r\n");
        client_printf(cl, "bytes\r\n");
        client_printf(cl, "lines\r\n");
        client_printf(cl, ".\r\n");
        return;
    }

    client_printf(cl, "501 keyword not recognized, see 7.6.2\r\n");
    client_flush(cl);
    return;
}

// See comments.c
unsigned str_count_newlines(const char *string);

void handle_xover_cmd(client_t *cl, const char *param)
{
    /* PARAM:
         Either: an article number
         Or: an article number followed by a dash to indicate all following
         Or: an article number followed by a dash followed by an article number
    */

    /* Output: CRLF-separated stream of
         number TAB subject TAB author TAB date TAB message-id TAB references TAB byte-count TAB line-count
    */
    json_object *object;
    char* endptr = NULL;
    char* references;
    int end = INT_MAX;
    if (!param) {
        client_send(cl, "420 No current article selected\r\n");
        return;
    }
    int beginning = strtol(param, &endptr, 10);
    if (endptr && *endptr == '-') {
        param = endptr + 1;
        if (*param) {
            end = strtol(param, &endptr, 10);
            if (endptr && *endptr != 0) {
                client_send(cl, "420 No article(s) selected\r\n");
                return;
            }
        }
    }

    client_send(cl, "224 Overview information follows\r\n");
    json_object_object_foreach(groupset, msg, artnum) {
        int i = json_object_get_int(artnum);
        if (i >= beginning && i <= end) {
            // Now we lookup that id in the spool file.
            if (json_object_object_get_ex(spool, msg, &object)) {
                json_object *data;
                json_object *created;
                time_t unixtime;
                char date[128];
                const char* body;
                int byte_count;
                unsigned line_count;
                if (!json_object_object_get_ex(object, "data", &data))
                    continue;

                if (!json_object_object_get_ex(data, "created_utc", &created))
                    continue;

                if (!json_object_is_type(created, json_type_double))
                    continue;

                // Convert that into a UNIX time.
                unixtime = json_object_get_int64(created);

                // RFC822 Format
                strftime(date, sizeof date, "%a, %d %b %Y %T %z", gmtime(&unixtime));

                if (article_generate_references(spool, object, &references) != 0)
                    references = "null";
                if (references && *references == 0)
                    references = "null";

                body = json_object_get_string_prop(data, "body");
                byte_count = body ? strlen(body) : 0;
                line_count = body ? str_count_newlines(body) : 0;

                client_printf(cl, "%d\t%s\t%s\t%s\t<%s@reddit>\t%s\t%d\t%u\r\n",
                                  i,
                                  json_object_get_string_prop(data, "title"),
                                  json_object_get_string_prop(data, "author"),
                                  date,
                                  msg,
                                  references,
                                  byte_count,
                                  line_count);
                // TODO: "\tXref: someserver somegroup:somenumber" at the end
            }
        }
    }
    client_printf(cl, ".\r\n");
}

void handle_newgroups_cmd(client_t *cl, const char *param)
{
    client_printf(cl, "231 new groups are not provided by nntpit\r\n");
    client_printf(cl, ".\r\n");
    client_flush(cl);
    return;
}

void handle_group_cmd(client_t *cl, const char *param)
{
    int highwm;
    int lowwm;

    if (!param) {
        client_printf(cl, "501 group must be specified, see 6.1.1.2\r\n");
        return;
    }

    if (fetch_subreddit_json(spool, newsrc, param) == 0) {
        g_debug("the fetch worked");

        // Save any updates to the spool or article map.
        reddit_spool_expunge(spool);

        json_object_to_file("newsrc", newsrc);
        json_object_to_file("spool", spool);
    }

    if (!json_object_object_get_ex(newsrc, param, &groupset)) {
        g_warning("unknown group: TODO: subscribe to it, this is like a command in slrn");
        client_printf(cl, "411 i dont have that group\r\n");
        return;
    }

    highwm = reddit_spool_highwatermark(groupset);
    lowwm  = reddit_spool_lowwatermark(groupset);

    client_printf(cl, "211 %d %d %d %s\r\n",
        highwm - lowwm,
        lowwm,
        highwm,
        param);

    client_flush(cl);
    return;
}

void handle_listgroup_cmd(client_t *cl, const char *param)
{
    int highwm;
    int lowwm;

    if (!param) {
        client_printf(cl, "501 group must be specified, see 6.1.1.2\r\n");
        return;
    }

    if (fetch_subreddit_json(spool, newsrc, param) == 0) {
        g_debug("the fetch worked");

        // Save any updates to the spool or article map.
        reddit_spool_expunge(spool);

        json_object_to_file("newsrc", newsrc);
        json_object_to_file("spool", spool);
    }

    if (!json_object_object_get_ex(newsrc, param, &groupset)) {
        g_warning("unknown group: TODO: subscribe to it, this is like a command in slrn");
        client_printf(cl, "411 i dont have that group\r\n");
        return;
    }

    highwm = reddit_spool_highwatermark(groupset);
    lowwm  = reddit_spool_lowwatermark(groupset);

    client_printf(cl, "211 %d %d %d %s\r\n",
        highwm - lowwm,
        lowwm,
        highwm,
        param);

    for (int i = lowwm; i < highwm; i++) {
        client_printf(cl, "%d\r\n", i);
    }

    client_printf(cl, ".\r\n");
    client_flush(cl);

    client_flush(cl);
    return;
}

void handle_head_cmd(client_t *cl, const char *param, bool head, bool article)
{
    json_object *object;
    char *endptr;
    char *headers;
    char *body;
    char *msgid;
    int number;
    int code;

    // List available newsgroups
    if (!param) {
        client_printf(cl, "501 no parameters, see 3.1.2\r\n");
        return;
    }

    if (!groupset) {
        client_printf(cl, "412 no newsgroup, see 3.1.2\r\n");
        return;
    }

    // Parse the number requested, note if *endptr == '<' it's a msgid.
    number = strtoul(param, &endptr, 10);

    object = NULL;

    // In slrn there is a get_parent_header command that uses this command
    // to rebuild threads.
    if (number == 0 && *endptr != '\0') {
        // Check that it looks like <msgid>
        if (*endptr != '<' || endptr[strlen(endptr) - 1] != '>') {
            client_printf(cl, "501 didnt understand, see 3.1.2\r\n");
            client_flush(cl);
            return;
        }

        // Extract the id.
        msgid = g_strndup(endptr + 1, strlen(endptr) - 2);

        // Now we lookup that id in the spool file.
        if (!json_object_object_get_ex(spool, msgid, &object)) {
            // Umm, I guess it was outdated?
            client_printf(cl, "423 sorry, couldnt find msg %s\r\n", msgid);
            g_free(msgid);
            return;
        }
    } else {
        json_object_object_foreach(groupset, msg, artnum) {
            if (json_object_get_int(artnum) == number) {

                // Now we lookup that id in the spool file.
                if (!json_object_object_get_ex(spool, msg, &object)) {
                    // Umm, I guess it was outdated?
                    client_printf(cl, "423 sorry, couldnt find that one\r\n");
                    return;
                }

                // We found the number requested.
                msgid = g_strdup(msg);

                break;
            }
        }
    }

    // If we reach here without a message, that we couldn't find it in the spool
    if (object == NULL) {
        client_printf(cl, "423 understood but couldnt find it, see 3.1.2\r\n");
        client_flush(cl);
        return;
    }

    // Now we need to translate that object into an RFC5536 message
    if (reddit_parse_comment(spool, object, &headers, &body) != 0) {
        client_printf(cl, "503 sorry, couldnt get the headers\r\n");
        return;
    }

    if (headers == NULL || body == NULL) {
        client_printf(cl, "503 sorry, failure generating message\r\n");
        g_free(headers);
        g_free(body);
        return;
    }

    // Figure out which response code is expected.
    if (head && article) {
        code = 220;
    }
    if (head && !article) {
        code = 221;
    }
    if (!head && article) {
        code = 222;
    }

    client_printf(cl, "%d %d <%s> message generated, text follows\r\n",
        code,
        number,
        msgid);

    if (head) {
        client_send(cl, headers);
    }

    if (head && article) {
        client_send(cl, "\r\n");
    }

    if (article) {
        client_send(cl, body);
        client_send(cl, "\r\n");
    }

    client_printf(cl, ".\r\n");
    client_flush(cl);
    g_free(headers);
    g_free(body);
    return;
}

void client_read(struct ev_loop *loop, ev_io *w, int revents)
{
    client_t  *cl = w->data;
    thread_t  *th = cl->cl_thread;
    char    *ln;
    ssize_t    n;

    if ((n = cq_read(cl->cl_rdbuf, cl->cl_fd)) == -1) {
        if (ignore_errno(errno))
            return;
        printf("[%d] read error: %s\n",
                cl->cl_fd, strerror(errno));
        client_close(cl);
        return;
    }

    if (n == 0) {
        client_close(cl);
        return;
    }

    while ((ln = cq_read_line(cl->cl_rdbuf))) {
        char  *cmd, *data;

        if (debug)
            printf("[%d] <- [%s]\n", cl->cl_fd, ln);

        /*
         * 238 <msg-id> -- CHECK, send the article
         * 431 <msg-id> -- CHECK, defer the article
         * 438 <msg-id> -- CHECK, never send the article
         * 239 <msg-id> -- TAKETHIS, accepted
         * 439 <msg-id> -- TAKETHIS, rejected
         * 335 <msg-id> -- IHAVE, send the article
         * 435 <msg-id> -- IHAVE, never send the article
         * 436 <msg-id> -- IHAVE, defer the article
         */

        if (cl->cl_state == CL_NORMAL) {
            cmd = ln;
            if ((data = index(cmd, ' ')) != NULL) {
                *data++ = 0;
                while (isspace(*data))
                    data++;
                if (!*data)
                    data = NULL;
            }

            if (strcasecmp(cmd, "LIST") == 0) {
                handle_list_cmd(cl, data);
            } else if (strcasecmp(cmd, "GROUP") == 0) {
                handle_group_cmd(cl, data);
            } else if (strcasecmp(cmd, "LISTGROUP") == 0) {
                handle_listgroup_cmd(cl, data);
            } else if (strcasecmp(cmd, "NEWGROUPS") == 0) {
                handle_newgroups_cmd(cl, data);
            } else if (strcasecmp(cmd, "HEAD") == 0) {
                handle_head_cmd(cl, data, true, false);
            } else if (strcasecmp(cmd, "ARTICLE") == 0) {
                handle_head_cmd(cl, data, true, true);
            } else if (strcasecmp(cmd, "BODY") == 0) {
                handle_head_cmd(cl, data, false, true);
            } else if (strcasecmp(cmd, "CAPABILITIES") == 0) {
                client_printf(cl,
                        "101 Capability list:\r\n"
                        "VERSION 2\r\n"
                        "IMPLEMENTATION nntpit %s\r\n", PACKAGE_VERSION);
                if (do_ihave)
                    client_send(cl, "IHAVE\r\n");
                if (do_streaming)
                    client_send(cl, "STREAMING\r\n");
                client_send(cl, ".\r\n");
            } else if (strcasecmp(cmd, "QUIT") == 0) {
                client_close(cl);
                reddit_spool_expunge(spool);
                json_object_to_file("newsrc", newsrc);
                json_object_to_file("spool", spool);
            } else if (strcasecmp(cmd, "MODE") == 0) {
                if (!data)
                    client_send(cl, "501 Unknown MODE.\r\n");
                else if (strcasecmp(data, "STREAM") == 0) {
                    if (do_streaming)
                        client_send(cl, "203 Streaming OK.\r\n");
                    else
                        client_send(cl, "501 Unknown MODE.\r\n");
                } else if (strcasecmp(data, "READER") == 0)
                    client_send(cl, "201 Hello, you can't post\r\n");
                else
                    client_send(cl, "501 Unknown MODE.\r\n");
            } else if (strcasecmp(cmd, "CHECK") == 0) {
                if (!do_streaming)
                    client_send(cl, "500 Unknown command.\r\n");
                else if (!data)
                    client_send(cl, "501 Missing message-id.\r\n");
                else {
                    th->th_nsend++;
                    client_printf(cl, "238 %s\r\n", data);
                }
            } else if (strcasecmp(cmd, "TAKETHIS") == 0) {
                if (!do_streaming)
                    client_send(cl, "500 Unknown command.\r\n");
                else if (!data)
                    client_send(cl, "501 Missing message-id.\r\n");
                else {
                    cl->cl_msgid = strdup(data);
                    cl->cl_state = CL_TAKETHIS;
                }
            } else if (strcasecmp(cmd, "IHAVE") == 0) {
                if (!do_ihave)
                    client_send(cl, "500 Unknown command.\r\n");
                else if (!data)
                    client_send(cl, "501 Missing message-id.\r\n");
                else {
                    client_printf(cl, "335 %s\r\n", data);
                    cl->cl_msgid = strdup(data);
                    cl->cl_state = CL_IHAVE;
                    th->th_nsend++;
                }
            } else if (strcasecmp(cmd, "XOVER") == 0) {
                handle_xover_cmd(cl, data);
            } else {
                client_printf(cl, "500 Unknown command (I saw %s).\r\n", cmd);
            }
        } else if (cl->cl_state == CL_TAKETHIS || cl->cl_state == CL_IHAVE) {
            if (strcmp(ln, ".") == 0) {
                client_printf(cl, "%d %s\r\n",
                        cl->cl_state == CL_IHAVE ? 235 : 239,
                        cl->cl_msgid);
                free(cl->cl_msgid);
                cl->cl_msgid = NULL;
                cl->cl_state = CL_NORMAL;
                th->th_naccepted++;
            }
        }

        free(ln);
        if (cl->cl_flags & CL_DEAD)
            return;
    }

    client_flush(cl);
}

void *
xmalloc(sz)
  size_t  sz;
{
void  *ret = malloc(sz);
  if (!ret) {
    fprintf(stderr, "out of memory\n");
    _exit(1);
  }

  return ret;
}

void *
xcalloc(n, sz)
  size_t  n, sz;
{
void  *ret = calloc(n, sz);
  if (!ret) {
    fprintf(stderr, "out of memory\n");
    _exit(1);
  }

  return ret;
}

void do_stats(struct ev_loop *loop, ev_timer *w, int revents)
{
    struct rusage rus;
    uint64_t ct;
    time_t upt = time(NULL) - start_time;

    pthread_mutex_lock(&stats_mtx);
    getrusage(RUSAGE_SELF, &rus);
    ct = (rus.ru_utime.tv_sec * 1000) + (rus.ru_utime.tv_usec / 1000)
        + (rus.ru_stime.tv_sec * 1000) + (rus.ru_stime.tv_usec / 1000);

    printf("send it: %d/s, refused: %d/s, rejected: %d/s, deferred: %d/s, accepted: %d/s, cpu %.2f%%\n",
            nsend, nrefuse, nreject, ndefer, naccept, (((double)ct / 1000) / upt) * 100);
    nsend = nrefuse = nreject = ndefer = naccept = 0;
    pthread_mutex_unlock(&stats_mtx);
}

void
thread_deadlist(struct ev_loop * loop, ev_prepare *w, int revents)
{
    client_t  *cl, *next;
    thread_t  *th = w->data;

    cl = th->th_deadlist;

    while (cl) {
        next = cl->cl_next;
        client_destroy(cl);
        cl = next;
    }

    th->th_deadlist = NULL;
}

void do_thread_stats(struct ev_loop *loop, ev_timer *w, int revents)
{
  thread_t  *th = w->data;
  pthread_mutex_lock(&stats_mtx);
  nsend += th->th_nsend;
  naccept += th->th_naccepted;
  ndefer += th->th_ndefer;
  nreject += th->th_nreject;
  nrefuse += th->th_nrefuse;
  pthread_mutex_unlock(&stats_mtx);

  th->th_nsend = th->th_naccepted = th->th_ndefer = th->th_nreject
    = th->th_nrefuse = 0;
}
