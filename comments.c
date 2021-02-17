#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include <json.h>
#include <glib.h>

#include "jsonutil.h"
#include "reddit.h"

int reddit_parse_listing(json_object *listing, json_object *props, json_object *newsrc);

void json_article_to_mbox(json_object *article)
{
    json_object *created;
    json_object *crossposts;
    time_t unixtime;
    char date[128];
    const char *body;

    if (!json_object_object_get_ex(article, "created_utc", &created)) {
        g_info("there was no created date");
        return;
    }

    if (!json_object_is_type(created, json_type_double)) {
        g_info("created time was not an integer");
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
    fprintf(stdout, "Message-Id: <%s>\n", json_object_get_string_prop(article, "name"));
    fprintf(stdout, "Newsgroups: %s", json_object_get_string_prop(article, "subreddit"));

    // Article could be crossposted, check for more groups.
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
    fprintf(stdout, "\n");
    fprintf(stdout, "%s", body);
    fprintf(stdout, "\n\n");
    return;
}

int reddit_parse_link(json_object *link, json_object *props, json_object *newsrc)
{
    json_object *data;

    if (!json_object_check_strprop(link, "kind", "t3", false)) {
        g_info("was expecting to be parsed a link object");
        return -1;
    }

    if (!json_object_object_get_ex(link, "data", &data)) {
        g_info("no data was found in the listing object");
        return -1;
    }

    // Record the last seen title.
    json_object_object_add(props, "title", json_object_new_string(json_object_get_string_prop(data, "title")));
    json_article_to_mbox(data);
    return 0;
}

static unsigned str_count_newlines(const char *string)
{
    unsigned count = 1;

    while ((string = strchr(string, '\n'))) {
        count++;
        string++;
    }

    return count;
}

// Parse the comment object specified, and return a news message
int reddit_parse_comment(json_object *spool, json_object *comment, char **headers, char **body)
{
    json_object *data;
    json_object *created;
    json_object *replies;
    json_object *currgroup;
    json_object *artmap;
    json_object *crossposts;
    int type;
    time_t unixtime;
    char date[128];

    // Initialize these in case something goes wrong.
    *headers = *body = NULL;

    // I think I can only handle comments and links.
    type = reddit_object_type(comment);

    if (type != REDDIT_OBJ_COMMENT && type != REDDIT_OBJ_LINK) {
        g_warning("was expecting to be parsed a comment or link object");
        return -1;
    }

    if (!json_object_object_get_ex(comment, "data", &data)) {
        g_warning("there was no data in the comment");
        return -1;
    }

    if (!json_object_object_get_ex(data, "created_utc", &created)) {
        g_warning("there was no created date in the comment");
        return -1;
    }

    if (!json_object_is_type(created, json_type_double)) {
        g_warning("created time was an unexpected type");
        return -1;
    }

    // Convert that into a UNIX time.
    unixtime = json_object_get_int64(created);

    // RFC822 Format
    strftime(date, sizeof date, "%a, %d %b %Y %T %z", gmtime(&unixtime));

    if (type == REDDIT_OBJ_COMMENT) {
        char *references;

        *body = g_strdup(json_object_get_string_prop(data, "body"));

        if (article_generate_references(spool, comment, &references) != 0) {
            g_warning("failed to generate a references header");
        }

        *headers = g_strdup_printf(
            "From: %s\r\n"
            "Subject: Re: %s\r\n"
            "Lines: %u\r\n"
            "Date: %s\r\n"
            "Message-Id: <%s>\r\n"
            "References: %s\r\n"
            "Newsgroups: %s\r\n"
            "Path: reddit!not-for-mail\r\n"
            "Content-Type: text/plain; charset=UTF-8\r\n",
            json_object_get_string_prop(data, "author"),
            json_object_get_string_prop(data, "title"),
            str_count_newlines(*body),
            date,
            json_object_get_string_prop(data, "name"),
            references,
            json_object_get_string_prop(data, "subreddit"));

        g_free(references);
    } else {
        char *newsgroups = g_strdup(json_object_get_string_prop(data, "subreddit"));

        if (json_object_object_get_ex(data, "crosspost_parent_list", &crossposts)) {
            for (size_t i = 0; i < json_object_array_length(crossposts); i++) {
                json_object *xpost = json_object_array_get_idx(crossposts, i);
                char *newlist = g_strdup_printf("%s,%s",
                                                newsgroups,
                                                json_object_get_string_prop(xpost, "subreddit"));

                // Found a new one, replace old list.
                g_free(newsgroups);

                // Copy new list.
                newsgroups = newlist;
            }
        }

        *body = g_strdup(json_object_get_string_prop(data, "selftext"));

        // Must be a link post?
        if (!*body || !**body) {
            g_free(*body);

            *body = g_strdup(json_object_get_string_prop(data, "url"));
        }

        *headers = g_strdup_printf(
            "From: %s\r\n"
            "Subject: %s\r\n"
            "Date: %s\r\n"
            "Lines: %u\r\n"
            "Message-Id: <%s>\r\n"
            "Newsgroups: %s\r\n"
            "Path: reddit!not-for-mail\r\n"
            "Content-Type: text/plain; charset=UTF-8\r\n",
            json_object_get_string_prop(data, "author"),
            json_object_get_string_prop(data, "title"),
            date,
            str_count_newlines(*body),
            json_object_get_string_prop(data, "name"),
            newsgroups);
        g_free(newsgroups);
    }
    return 0;
}
