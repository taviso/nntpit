// This file is part of nntpit, https://github.com/taviso/nntpit.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <json.h>
#include <glib.h>

#include "json_object.h"
#include "reddit.h"

struct MemoryStruct {
  char *memory;
  size_t size;
};

static size_t
WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp)
{
  size_t realsize = size * nmemb;
  struct MemoryStruct *mem = (struct MemoryStruct *)userp;

  char *ptr = realloc(mem->memory, mem->size + realsize + 1);
  if(ptr == NULL) {
    /* out of memory! */
    printf("not enough memory (realloc returned NULL)\n");
    return 0;
  }

  mem->memory = ptr;
  memcpy(&(mem->memory[mem->size]), contents, realsize);
  mem->size += realsize;
  mem->memory[mem->size] = 0;

  return realsize;
}

int fetch_subreddit_json(json_object *spool, json_object *newsrc, const char *group)
{
  CURL *curl_handle;
  json_object *subreddit;
  CURLcode res;
  char *url;

  struct MemoryStruct chunk;

  chunk.memory = malloc(1);  /* will be grown as needed by the realloc above */ 
  chunk.size = 0;    /* no data at this point */ 

  curl_global_init(CURL_GLOBAL_ALL);

  /* init the curl session */ 
  curl_handle = curl_easy_init();

  url = g_strdup_printf("https://www.reddit.com/r/%s.json", group);

  /* specify URL to get */ 
  curl_easy_setopt(curl_handle, CURLOPT_URL, url);

  /* send all data to this function  */ 
  curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);

  /* we pass our 'chunk' struct to the callback function */ 
  curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)&chunk);

  /* some servers don't like requests that are made without a user-agent
     field, so we provide one */ 
  curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "nntpreader/1.0");

  /* get it! */ 
  res = curl_easy_perform(curl_handle);

  /* check for errors */ 
  if (res != CURLE_OK) {
    g_warning("curl_easy_perform() failed: %s", curl_easy_strerror(res));
  } else {
    g_debug("%lu bytes retrieved, %.8s...", chunk.size, chunk.memory);

    subreddit = json_tokener_parse(chunk.memory);

    if (subreddit != NULL) {
        json_object *data;
        json_object *children;

        g_warn_if_fail(reddit_object_type(subreddit) == REDDIT_OBJ_LISTING);
        // The question is, are there any new comments we don't know about?

        // For every object we just fetched, compare the number of comments to
        // the number of comments we already knew about.
        if (!json_object_object_get_ex(subreddit, "data", &children)) {
            g_warning("no data was found in the listing object");
            goto parseerror;
        }

        if (!json_object_object_get_ex(children, "children", &children)) {
            g_warning("no child objects found in the listing");
            goto parseerror;
        }

        if (!json_object_is_type(children, json_type_array)) {
            g_info("expecting children to be an array of objects");
            goto parseerror;
        }

        for (size_t i = 0; i < json_object_array_length(children); i++) {
            json_object *child = json_object_array_get_idx(children, i);
            json_object *origdata;
            json_object *newdata;
            json_object *orig;
            json_object *origcomments;
            json_object *newcomments;

            // Lookup if this id is in the spool
            if (!reddit_spool_retrieve(spool, reddit_object_id(child), &orig)) {
                // I don't know this article, so we definitely need it.
                fetch_comments_json(spool, newsrc, group, reddit_object_id(child));
                continue;
            }

            // Check if there are new comments since we last looked.
            json_object_object_get_ex(child, "data", &newdata);
            json_object_object_get_ex(orig, "data", &origdata);
            json_object_object_get_ex(origdata, "num_comments", &origcomments);
            json_object_object_get_ex(newdata, "num_comments", &newcomments);

            if (json_object_get_int(origcomments) < json_object_get_int(newcomments)) {
                // There are new comments.
                g_debug("story %s has %u vs %u known comments => re-fetch",
                        reddit_object_id(child),
                        json_object_get_int(origcomments),
                        json_object_get_int(newcomments));

                fetch_comments_json(spool, newsrc, group, reddit_object_id(child));
                continue;
            }
        }

        // Merge every known object with the spool.
        reddit_spool_merge_object(spool, subreddit);

        // Update our article ids.
        reddit_spool_maparticles(spool, group, newsrc);
      parseerror:
        // Done with this object.
        json_object_put(subreddit);
    } else {
        g_warning("failed to parse subreddit json");
    }
  }

  /* cleanup curl stuff */ 
  curl_easy_cleanup(curl_handle);

  free(chunk.memory);

  g_free(url);

  /* we're done with libcurl, so clean it up */ 
  curl_global_cleanup();

  return res == CURLE_OK ? 0 : -1;
}

int fetch_comments_json(json_object *spool, json_object *newsrc, const char *group, const char *id)
{
  CURL *curl_handle;
  json_object *comments;
  CURLcode res;
  char *url;

  struct MemoryStruct chunk;

  chunk.memory = malloc(1);  /* will be grown as needed by the realloc above */ 
  chunk.size = 0;    /* no data at this point */ 

  curl_global_init(CURL_GLOBAL_ALL);

  /* init the curl session */ 
  curl_handle = curl_easy_init();

  url = g_strdup_printf("https://www.reddit.com/r/%s/comments/%s.json", group, id + 3);

  g_debug("url is %s", url);

  /* specify URL to get */ 
  curl_easy_setopt(curl_handle, CURLOPT_URL, url);

  /* send all data to this function  */ 
  curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);

  /* we pass our 'chunk' struct to the callback function */ 
  curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)&chunk);

  /* some servers don't like requests that are made without a user-agent
     field, so we provide one */ 
  curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "nntpreader/1.0");

  /* get it! */ 
  res = curl_easy_perform(curl_handle);

  /* check for errors */ 
  if (res != CURLE_OK) {
    g_warning("curl_easy_perform() failed: %s", curl_easy_strerror(res));
  } else {
    g_debug("%lu bytes retrieved, %.8s...", chunk.size, chunk.memory);

    comments = json_tokener_parse(chunk.memory);

    if (comments != NULL) {
        // Merge every known object with the spool.
        reddit_spool_merge_object(spool, comments);

        // Update our article ids.
        reddit_spool_maparticles(spool, group, newsrc);
      parseerror:
        // Done with this object.
        json_object_put(comments);
    } else {
        g_warning("failed to parse subreddit json");
    }
  }

  /* cleanup curl stuff */ 
  curl_easy_cleanup(curl_handle);

  free(chunk.memory);

  g_free(url);

  /* we're done with libcurl, so clean it up */ 
  curl_global_cleanup();

  return res == CURLE_OK ? 0 : -1;
}
