#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include <json.h>
#include <glib.h>

#include "json_object.h"
#include "jsonutil.h"
#include "reddit.h"

// Articles older than this get expunged.
#define MAX_SPOOL_AGE (60 * 60 * 24 * 14)

int reddit_comment_add_title(json_object *spool, json_object *comment)
{
    const char *linkid;
    const char *subject;
    json_object *parent;
    json_object *parentdata;
    json_object *commentdata;

    if (reddit_object_type(comment) != REDDIT_OBJ_COMMENT) {
        return -1;
    }

    json_object_object_get_ex(comment, "data", &commentdata);

    // Already has a topic?
    if (json_object_get_string_prop(commentdata, "title")) {
        g_debug("comment already had a title, no need to append another.");
        return 0;
    }

    // Reformat it to the format expected.
    linkid = json_object_get_string_prop(commentdata, "link_id");

    if (!reddit_spool_retrieve(spool, linkid, &parent)) {
        g_debug("cant find parent %s, what happened?", linkid);
        return -1;
    }

    // Insert it.
    json_object_object_get_ex(parent, "data", &parentdata);

    subject = json_object_get_string_prop(parentdata, "title");

    json_object_object_add(commentdata, "title", json_object_new_string(subject));

    return 0;
}

// Add the comment or link object to the spool.
int reddit_spool_store(json_object *spool, json_object *object)
{
    int type = reddit_object_type(object);
    const char *id = reddit_object_id(object);
    json_object *replies;
    json_object *data;

    if (type == REDDIT_OBJ_MORE) {
        g_debug("TODO: handle 'more' objects");
        return -1;
    }

    g_warn_if_fail(type == REDDIT_OBJ_LINK || type == REDDIT_OBJ_COMMENT);

    if (id == NULL) {
        g_warning("attempted to spool non-reddit object");
        return -1;
    }

    // Is this object already in the spool?
    json_object_object_add(spool, id, object);

    if (!json_object_object_get_ex(object, "data", &data)) {
        g_warning("badly formed object, expected a data property");
        return -1;
    }

    // Increment reference count.
    json_object_get(object);

    // Add a timestamp.
    json_object_object_add(object, "timestamp", json_object_new_int64(time(0)));

    g_debug("added object %s to spoolfile", id);

    // Add a topic if it needs one.
    reddit_comment_add_title(spool, object);

    // Comments have a replies object, so we need to parse that too.
    if (json_object_object_get_ex(data, "replies", &replies)) {
        if (json_object_is_type(replies, json_type_object)) {
            g_debug("object had a replies property, attempting to parse.");
            return reddit_spool_merge_object(spool, replies);
        }

        // I think it must be an empty string then?
        g_warn_if_fail(json_object_is_type(replies, json_type_string));
    }

    return 0;
}

int reddit_spool_retrieve(json_object *spool, const char *id, json_object **object)
{
    return json_object_object_get_ex(spool, id, object);
}

int reddit_spool_expunge(json_object *spool)
{
    json_object_object_foreach(spool, id, article) {
        json_object *timestamp;
        json_object_object_get_ex(article, "timestamp", &timestamp);
        if (json_object_get_int64(timestamp) - time(0) > MAX_SPOOL_AGE) {
            json_object_object_del(article, id);
        }
    }
    // TODO: also expunge old mappings in newsrc
    return 0;
}

// Add all of the reddit objects (e.g. comments) in object to spool.
int reddit_spool_merge_object(json_object *spool, json_object *object)
{
    // Comment objects are sometimes served as an array, so handle that here.
    if (json_object_is_type(object, json_type_array)) {
        g_debug("parsed an array, assuming it's an array of objects");

        for (size_t i = 0; i < json_object_array_length(object); i++) {
            json_object *child = json_object_array_get_idx(object, i);

            if (reddit_spool_merge_object(spool, child) != 0) {
                g_info("failed to merge a child object");
                return -1;
            }
        }

        return 0;
    }

    if (reddit_object_type(object) == REDDIT_OBJ_LISTING) {
        json_object *data;
        json_object *children;

        if (!json_object_object_get_ex(object, "data", &children)) {
            g_warning("no data was found in the listing object");
            return -1;
        }

        if (!json_object_object_get_ex(children, "children", &children)) {
            g_warning("no child objects found in the listing");
            return -1;
        }

        if (!json_object_is_type(children, json_type_array)) {
            g_info("expecting children to be an array of objects");
            return -1;
        }

        for (size_t i = 0; i < json_object_array_length(children); i++) {
            json_object *child = json_object_array_get_idx(children, i);

            if (reddit_spool_merge_object(spool, child) != 0) {
                g_info("failed to merge a child object");
                return -1;
            }
        }

        // Listing complete.
        return 0;
    }

    return reddit_spool_store(spool, object);
}

int reddit_spool_highwatermark(json_object *groupmap)
{
    int watermark = 0;

    json_object_object_foreach(groupmap, id, number) {
        if (json_object_get_int(number) > watermark)
            watermark = json_object_get_int(number);
    }

    return watermark;
}

int reddit_spool_lowwatermark(json_object *groupmap)
{
    int watermark = 0;

    json_object_object_foreach(groupmap, id, number) {
        if (json_object_get_int(number) < watermark)
            watermark = json_object_get_int(number);
    }

    return watermark;
}


int reddit_spool_maparticles(json_object *spool, const char *subreddit, json_object *newsrc)
{
    json_object *groupmap;
    int watermark;

    if (!json_object_object_get_ex(newsrc, subreddit, &groupmap)) {
        g_debug("group %s was not in the article map, I'm adding it", subreddit);

        groupmap = json_object_new_object();
        json_object_object_add(newsrc, subreddit, groupmap);
    }

    // Learn the high watermark (this is a nntp concept)
    watermark = reddit_spool_highwatermark(groupmap);

    g_debug("the current high watermark for %s is %d", subreddit, watermark);

    // Now check for any articles in this group.
    json_object_object_foreach(spool, key, article) {
        int type = reddit_object_type(article);
        json_object *data;

        if (!json_object_object_get_ex(article, "data", &data)) {
            g_warning("failed to find the data element of a spool entry, corrupted?");
            continue;
        }

        if (type == REDDIT_OBJ_MORE) {
            g_debug("TODO: handle more objects");
            continue;
        }

        g_warn_if_fail(type == REDDIT_OBJ_LINK || type == REDDIT_OBJ_COMMENT);

        // Check if this belongs to this group.
        if (json_object_check_strprop(data, "subreddit", subreddit, false)) {
            // We found one, check if he's in the groupmap.
            if (!json_object_object_get_ex(groupmap, key, NULL)) {
                // He's not in there, add it.
                json_object_object_add(groupmap, key, json_object_new_int(++watermark));
            }
        }
    }

    g_debug("finished searching, high watermark for %s is now %d",
            subreddit,
            reddit_spool_highwatermark(groupmap));

    return 0;
}
