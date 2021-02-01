#ifndef __JSONUTIL_H
#define __JSONUTIL_H

bool json_object_check_strprop(json_object *obj,
                               const char *property,
                               const char *expected,
                               bool casesensitive);
const char *json_object_get_string_prop(json_object *obj,
                                        char *property);
#endif
