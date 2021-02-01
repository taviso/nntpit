// This file is part of nntpit, https://github.com/taviso/nntpit.

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include <json.h>
#include <glib.h>

#include "jsonutil.h"

bool json_object_check_strprop(json_object *obj,
                               const char *property,
                               const char *expected,
                               bool casesensitive)
{
    json_object *strprop;

    if (!json_object_object_get_ex(obj, property, &strprop)) {
        g_info("requested property %s didnt exist", property);
        return false;
    }

    if (!json_object_is_type(strprop, json_type_string)) {
        g_info("requested property %s is not a string", property);
        return false;
    }

    if (casesensitive) {
        return strcmp(json_object_get_string(strprop), expected) == 0;
    }

    return strcasecmp(json_object_get_string(strprop), expected) == 0;
}

const char *json_object_get_string_prop(json_object *obj, char *property)
{
    json_object *strprop;

    if (!json_object_object_get_ex(obj, property, &strprop)) {
        return NULL;
    }

    if (!json_object_is_type(strprop, json_type_string)) {
        return NULL;
    }

    return json_object_get_string(strprop);
}
