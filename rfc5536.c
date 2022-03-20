#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include <json.h>
#include <glib.h>

#include "jsonutil.h"
#include "reddit.h"

int article_generate_references(json_object *spool, json_object *object, char **references)
{
    json_object *data;

    // Initialize result.
    *references = g_strdup("");

    // A link object is simple to handle, there are no references.
    if (reddit_object_type(object) == REDDIT_OBJ_LINK) {
        return 0;
    }

    if (reddit_object_type(object) != REDDIT_OBJ_COMMENT) {
        g_debug("cant generate references for this object type");
        return -1;
    }

    // Now we try to build a list of all the references this article has.
    for (json_object *parent = object; true;) {
        const char *parentid;
        char *refs;

        // We must be at the top of the chain.
        if (reddit_object_type(parent) != REDDIT_OBJ_COMMENT) {
            g_warn_if_fail(reddit_object_type(parent) == REDDIT_OBJ_LINK);
            break;
        }

        // Fetch the contents.
        if (!json_object_object_get_ex(parent, "data", &data)) {
            g_warning("there was no data in a comment, unexpected");
            return -1;
        }

        // Find out the next comment in the chain.
        parentid = json_object_get_string_prop(data, "parent_id");

        // Append that to the list.
        refs = g_strdup_printf("<%s@reddit>%s%s",
            parentid,
            **references != '\0' ? " " : "",
            *references);

        // Free the old header.
        g_free(*references);

        // Use the new one.
        *references = refs;

        // Now fetch the parent.
        if (reddit_spool_retrieve(spool, parentid, &parent) != true) {
            g_debug("failed to complete chain, might be incomplete");
            break;
        }
    }

    // Header complete.
    g_debug("%s references: %s", reddit_object_id(object), *references);

    return 0;
}
