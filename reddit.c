#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include <json.h>
#include <glib.h>

#include "reddit.h"
#include "jsonutil.h"

// The alphabet reddit uses for id encoding.
static const char kidAlphabet[] = "0123456789abcdefghijklmnopqrstuvwxyz";

const char *
reddit_object_id(json_object *object)
{
    json_object *data;

    if (json_object_object_get_ex(object, "data", &data)) {
        return json_object_get_string_prop(data, "name");
    }

    g_warning("parsed a non-reddit object, expected a data property");

    return NULL;
}

int
reddit_object_type(json_object *obj)
{
    if (json_object_check_strprop(obj, "kind", "listing", false))
        return REDDIT_OBJ_LISTING;

    if (json_object_check_strprop(obj, "kind", "t1", false))
        return REDDIT_OBJ_COMMENT;

    if (json_object_check_strprop(obj, "kind", "t3", false))
        return REDDIT_OBJ_LINK;

    if (json_object_check_strprop(obj, "kind", "t2", false))
        return REDDIT_OBJ_ACCOUNT;

    if (json_object_check_strprop(obj, "kind", "t4", false))
        return REDDIT_OBJ_MESSAGE;

    if (json_object_check_strprop(obj, "kind", "t5", false))
        return REDDIT_OBJ_SUBREDDIT;

    if (json_object_check_strprop(obj, "kind", "t6", false))
        return REDDIT_OBJ_AWARD;

    if (json_object_check_strprop(obj, "kind", "t8", false))
        return REDDIT_OBJ_PROMO;

    g_warning("unexpected object kind found %s", json_object_get_string_prop(obj, "kind"));

    return -1;
}

unsigned reddit_decode_id(const char *idstr)
{
    long id;

    for (id = 0; *idstr; idstr++) {
        uint8_t val = index(kidAlphabet, *idstr) - kidAlphabet;
        id = id * 36 + val;
    }

    return id;
}

int reddit_encode_id(unsigned id, char idstr[8])
{
    ldiv_t result = {
        .quot = id
    };

    // I copied this algorithm from str_to_base() in reddit code, just
    // translated it into c.
    for (*idstr = 0; result.quot != 0;) {
        result = ldiv(result.quot, 36);

        // Make room at the start of the string.
        memmove(idstr + 1, idstr, strlen(idstr) + 1);

        // Insert a new character.
        *idstr = kidAlphabet[result.rem];
    }

    return 0;
}
