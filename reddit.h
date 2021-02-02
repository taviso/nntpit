#ifndef __REDDIT_H
#define __REDDIT_H

enum {
    REDDIT_OBJ_LISTING,
    REDDIT_OBJ_COMMENT,
    REDDIT_OBJ_LINK,
    REDDIT_OBJ_ACCOUNT,
    REDDIT_OBJ_MESSAGE,
    REDDIT_OBJ_SUBREDDIT,
    REDDIT_OBJ_AWARD,
    REDDIT_OBJ_PROMO,
    REDDIT_OBJ_MORE,
};

int
reddit_object_type(json_object *obj);

const char *
reddit_object_id(json_object *obj);

int
reddit_spool_store(json_object *spool, json_object *object);

int
reddit_spool_retrieve(json_object *spool, const char *id, json_object **object);

int
reddit_spool_expunge(json_object *spool);

unsigned
reddit_decode_id(const char *idstr);

int
reddit_encode_id(unsigned id, char idstr[8]);

int
reddit_spool_merge_object(json_object *spool, json_object *object);

int
reddit_spool_maparticles(json_object *spool, const char *subreddit, json_object *newsrc);

int
reddit_spool_highwatermark(json_object *groupmap);

int
reddit_spool_lowwatermark(json_object *groupmap);

int
reddit_parse_comment(json_object *comment,
                     char **headers,
                     char **body);

int
fetch_subreddit_json(json_object *spool, json_object *newsrc, const char *url);

int
fetch_comments_json(json_object *spool, json_object *newsrc, const char *group, const char *id);


#endif
