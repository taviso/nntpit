/*
 * This file is part of nntpit, https://github.com/taviso/nntpit.
 *
 * Based on nntpsink, Copyright (c) 2011-2014 Felicity Tarnell.
 */



#ifndef NTS_CHARQ_H
#define NTS_CHARQ_H

#include  <sys/types.h>

#include  "queue.h"

/*
 * A charq is a simple char buffer that allows efficient addition of data at the
 * end and removal of data at the start.  It can be used to implement network
 * buffering.
 *
 * A charq is actually a deque, but only queue operations are provided.
 */

#define CHARQ_BSZ 16384

typedef struct charq_ent {
  TAILQ_ENTRY(charq_ent)  cqe_list;
  char      cqe_data[CHARQ_BSZ];
} charq_ent_t;

typedef TAILQ_HEAD(charq_ent_list, charq_ent) charq_ent_list_t;

typedef struct charq {
  size_t     cq_len;  /* Amount of data in q */
  size_t     cq_offs; /* Unused space in the first ent */
  charq_ent_list_t cq_ents; /* List of ents */
} charq_t;

#define cq_len(cq)    ((cq)->cq_len)
#define cq_used(cq)   ((cq)->cq_len + (cq)->cq_offs)
#define cq_nents(cq)    (TAILQ_EMPTY(&cq->cq_ents) ? 0 : ((cq_used(cq) + CHARQ_BSZ - 1) / CHARQ_BSZ))
#define cq_left(cq)   (cq_nents(cq) * CHARQ_BSZ - cq_used(cq))
#define cq_first_ent(cq)  (TAILQ_FIRST(&(cq)->cq_ents))
#define cq_last_ent(cq)   (TAILQ_LAST(&(cq)->cq_ents, charq_ent_list))
#define cq_last_ent_free(cq)  (cq_last_ent(cq)->cqe_data + (CHARQ_BSZ - cq_left(cq)))

void   cq_init(void);

charq_t *cq_new(void);
void   cq_free(charq_t *);

ssize_t  cq_write(charq_t *, int);
ssize_t  cq_read(charq_t *, int);

void   cq_append(charq_t *, char const *, size_t);
void   cq_remove_start(charq_t *, size_t);
void   cq_extract_start(charq_t *, void *buf, size_t);

char   *cq_read_line(charq_t *);

#endif  /* !NTS_CHARQ_H */
