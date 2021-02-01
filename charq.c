/* 
 * This file is part of nntpit, https://github.com/taviso/nntpit.
 *
 * Based on nntpsink, Copyright (c) 2011-2014 Felicity Tarnell.
 *
 */

#include  <sys/uio.h>

#include  <stdlib.h>
#include  <strings.h>
#include  <unistd.h>
#include  <errno.h>
#include  <assert.h>

#include  "charq.h"
#include  "nntpit.h"

charq_t *
cq_new()
{
charq_t   *cq = xcalloc(1, sizeof(*cq));
  TAILQ_INIT(&cq->cq_ents);
  return cq;
}

void
cq_free(cq)
  charq_t *cq;
{
charq_ent_t *cqe;
  while (cqe = TAILQ_FIRST(&cq->cq_ents)) {
    TAILQ_REMOVE(&cq->cq_ents, cqe, cqe_list);
    free(cqe);
  }
  free(cq);
}

void
cq_append(cq, data, sz)
  charq_t   *cq;
  char const  *data;
  size_t     sz;
{
  if (!TAILQ_EMPTY(&cq->cq_ents)) {
  size_t     todo = sz > cq_left(cq) ? cq_left(cq) : sz;
    bcopy(data, cq_last_ent_free(cq), todo);
    sz -= todo;
    data += todo;
    cq->cq_len += todo;
  }

  while (sz) {
  charq_ent_t *new;
  size_t     todo = sz > CHARQ_BSZ ? CHARQ_BSZ : sz;
    new = calloc(1, sizeof(*new));
    bcopy(data, new->cqe_data, todo);
    cq->cq_len += todo;
    sz -= todo;
    data += todo;
    TAILQ_INSERT_TAIL(&cq->cq_ents, new, cqe_list);
  }
}

void
cq_remove_start(charq_t *cq, size_t sz)
{
    assert(sz <= cq_len(cq));
    while (sz >= (CHARQ_BSZ - cq->cq_offs)) {
        charq_ent_t *n = cq_first_ent(cq);
        TAILQ_REMOVE(&cq->cq_ents, n, cqe_list);
        free(n);
        cq->cq_len -= (CHARQ_BSZ - cq->cq_offs);
        sz -= (CHARQ_BSZ - cq->cq_offs);
        cq->cq_offs = 0;
    }

    cq->cq_offs += sz;
    cq->cq_len -= sz;
}

ssize_t
cq_write(charq_t *cq, int fd)
{
    ssize_t   i = 0;
    charq_ent_t *first = cq_first_ent(cq);

    while (cq_len(cq)) {
        ssize_t n;
        first = cq_first_ent(cq);
        n = write(fd, first->cqe_data + cq->cq_offs, 
                cq_nents(cq) > 1 
                ? (CHARQ_BSZ - cq->cq_offs)
                : cq_len(cq));
        if (n <= 0)
            return n;

        cq_remove_start(cq, n);
        i += n;
    }

    return i;
}

void
cq_extract_start(cq, buf, len)
  charq_t *cq;
  void  *buf;
  size_t   len;
{
unsigned char *bufp = buf;
charq_ent_t *first = cq_first_ent(cq);

  while (len && cq_len(cq)) {
  ssize_t n;
    first = cq_first_ent(cq);
    n = cq_nents(cq) > 1 
      ? (CHARQ_BSZ - cq->cq_offs)
      : cq_len(cq);
    if (n > len)
      n = len;
    bcopy(first->cqe_data + cq->cq_offs,
          bufp, n);
    len -= n;
    bufp += n;
    cq_remove_start(cq, n);
  }
}

ssize_t
cq_read(charq_t *cq, int fd)
{
    ssize_t n;  
    if (cq_left(cq) == 0) {
        charq_ent_t *cqe;
        cqe = xcalloc(1, sizeof(*cqe));
        n = read(fd, cqe->cqe_data, CHARQ_BSZ);
        if (n <= 0) {
            if (n == -1 && errno == EINVAL)
                abort();
            free(cqe);
            return n;
        }
        cq->cq_len += n;
        TAILQ_INSERT_TAIL(&cq->cq_ents, cqe, cqe_list);
        return n;
    }

    n = read(fd, cq_last_ent_free(cq), cq_left(cq));
    if (n == -1 && errno == EINVAL)
        abort();
    if (n > 0)
        cq->cq_len += n;
    return n;
}

static ssize_t
cq_find(cq, c)
  charq_t *cq;
  char   c;
{
char    *p;
charq_ent_t *e;
size_t     i = 0, flen;

  if (cq_len(cq) == 0)
    return -1;
  
  flen = CHARQ_BSZ - cq->cq_offs;
  if (flen > cq_len(cq))
    flen = cq_len(cq);

  for (e = cq_first_ent(cq), p = e->cqe_data + cq->cq_offs;
       p < (e->cqe_data + cq->cq_offs + flen); p++) {
    if (*p == c)
      return p - (e->cqe_data + cq->cq_offs);
  }
  i = CHARQ_BSZ - cq->cq_offs;
  for (e = TAILQ_NEXT(e, cqe_list); e; e = TAILQ_NEXT(e, cqe_list)) {
    for (p = e->cqe_data; i < cq->cq_len && p < (e->cqe_data + CHARQ_BSZ); p++, i++) {
      if (*p == c) {
        return i;
      }
    }
  }
  return -1;
}

char *
cq_read_line(cq)
  charq_t *cq;
{
charq_ent_t *e;
ssize_t    pos;
char    *line;
size_t     done = 0, todo;

  if ((pos = cq_find(cq, '\n')) == -1)
    return NULL;
  pos++;
  line = xmalloc(pos + 1);

  e = cq_first_ent(cq);
  todo = pos <= (CHARQ_BSZ - cq->cq_offs) ?
    pos : (CHARQ_BSZ - cq->cq_offs);
  assert(todo <= pos);
  bcopy(e->cqe_data + cq->cq_offs, line, todo);
  done += todo;

  if (done == pos) {
    line[done - 1] = 0;
    if (*line && line[done - 2] == '\r')
      line[done - 2] = 0;
    cq_remove_start(cq, pos);
    return line;
  }

  for (e = TAILQ_NEXT(e, cqe_list); e; e = TAILQ_NEXT(e, cqe_list)) {
    todo = pos <= CHARQ_BSZ ?  pos : CHARQ_BSZ;
    if (todo > (pos - done))
      todo = pos - done;
    bcopy(e->cqe_data, line + done, todo);
    done += todo;
    if (done == pos) {
      line[done - 1] = 0;
      if (*line && line[done - 2] == '\r')
        line[done - 2] = 0;
      cq_remove_start(cq, pos);
      return line;
    }
  }

  abort();
}
