#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include <json.h>

#include "jsonutil.h"

#define log_error(...) do {                 \
    fprintf(stderr, "error: "__VA_ARGS__);  \
    fprintf(stderr, "\n");                  \
} while(false);

static void json_article_to_mbox(json_object *article)
{
    json_object *created;
    json_object *crossposts;
    time_t unixtime;
    char date[128];
    const char *body;

    if (!json_object_object_get_ex(article, "created_utc", &created)) {
        log_error("there was no created date");
        return;
    }

    if (!json_object_is_type(created, json_type_double)) {
        log_error("created time was not an integer");
        return;
    }

    // Convert that into a UNIX time.
    unixtime = json_object_get_int64(created);

    // RFC822 Format
    strftime(date, sizeof date, "%a, %d %b %Y %T %z", gmtime(&unixtime));

    body = json_object_get_string_prop(article, "selftext");

    // Must be a link post?
    if (!body || !*body) {
        body = json_object_get_string_prop(article, "url");
    }


    fprintf(stdout, "From reddit Fri Jan 1 00:00:00 2000\n");
    fprintf(stdout, "From: %s\n", json_object_get_string_prop(article, "author"));
    fprintf(stdout, "Date: %s\n",  date);
    fprintf(stdout, "Subject: %s\n", json_object_get_string_prop(article, "title"));
    fprintf(stdout, "Message-Id: <%s@reddit>\n", json_object_get_string_prop(article, "name"));
    fprintf(stdout, "Newsgroups: %s", json_object_get_string_prop(article, "subreddit")); // TODO: crosspost_parent_list

    // Crossposted?
    if (json_object_object_get_ex(article, "crosspost_parent_list", &crossposts)) {
        for (size_t i = 0; i < json_object_array_length(crossposts); i++) {
            json_object *xpost = json_object_array_get_idx(crossposts, i);
            fprintf(stdout, ",%s", json_object_get_string_prop(xpost, "subreddit"));
        }
    }

    fprintf(stdout, "\n");

    fprintf(stdout, "Path: reddit!not-for-mail\n");
    fprintf(stdout, "Content-Type: text/plain; charset=UTF-8\n");
    fprintf(stdout, "Content-Length: %lu\n", strlen(body));
    fprintf(stdout, "X-Reddit-URL: %s\n", json_object_get_string_prop(article, "url"));
    fprintf(stdout, "\n");
    fprintf(stdout, "%s", body);
    fprintf(stdout, "\n\n");
    return;
}

/*
int main(int argc, char **argv)
{
    json_object *data;
    json_object *children;
    json_object *subreddit = json_object_from_file(argv[1]);

    if (!json_object_check_strprop(subreddit, "kind", "listing", false)) {
        log_error("failed to find expected kind");
        return -1;
    }

    if (!json_object_object_get_ex(subreddit, "data", &data)) {
        log_error("no data was found in the listing");
        return -1;
    }

    if (!json_object_is_type(data, json_type_object)) {
        log_error("the data object wasn't the right type");
        return -1;
    }

    if (!json_object_object_get_ex(data, "children", &children)) {
        log_error("the data object had no children property");
        return -1;
    }

    if (!json_object_is_type(children, json_type_array)) {
        log_error("the children property wasnt an array");
        return -1;
    }

    if (json_object_array_length(children) <= 0) {
        log_error("the children object was an empty array");
        return -1;
    }

    for (size_t i = 0; i < json_object_array_length(children); i++) {
        json_object *article = json_object_array_get_idx(children, i);
        json_object *data;

        if (!json_object_check_strprop(article, "kind", "t3", false)) {
            log_error("failed to find expected kind");
            return -1;
        }

        if (!json_object_object_get_ex(article, "data", &data)) {
            log_error("no data was found in the listing");
            return -1;
        }

        json_article_to_mbox(data);
    }

    json_object_put(subreddit);
    return 0;
}
*/
