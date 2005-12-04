/*
 *  arch-tag: Implementation of podcast parse
 *
 *  Copyright (C) 2005 Renato Araujo Oliveira Filho - INdT <renato.filho@indt.org.br>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 */

#define _XOPEN_SOURCE
#include <time.h>
#include <libxml/entities.h>
#include <libxml/SAX.h>
#include <libxml/parserInternals.h>
#include <libgnomevfs/gnome-vfs.h>
#include <string.h>

#include "rb-debug.h"
#include "rb-podcast-parse.h" 

#define BUFFER_SIZE 256

struct RBPoadcastLoadContext
{
    guint in_unknown_elt;
    xmlParserCtxtPtr xmlctx;
    GString *prop_value;
    RBPodcastChannel *channel_data;
    RBPodcastItem *item_data;
    
    enum {
        RB_PODCAST_PARSER_STATE_START,
        RB_PODCAST_PARSER_STATE_RSS,
        RB_PODCAST_PARSER_STATE_CHANNEL,
        RB_PODCAST_PARSER_STATE_CHANNEL_PROPERTY,
        RB_PODCAST_PARSER_STATE_IMG,
        RB_PODCAST_PARSER_STATE_IMG_PROPERTY,
        RB_PODCAST_PARSER_STATE_ITEM,
        RB_PODCAST_PARSER_STATE_ITEM_PROPERTY,
        RB_PODCAST_PARSER_STATE_END,
    } state;
};

static gboolean rb_validate_channel_propert (const char *name);
static gboolean rb_validate_item_propert (const char *name);
static uintmax_t rb_podcast_parse_date (const char* date_str);
static gulong rb_podcast_parse_time (const char *time_str);
static void rb_podcast_parser_start_element (struct RBPoadcastLoadContext* ctx, const char *name, const char **attrs);
static void rb_podcast_parser_end_element (struct RBPoadcastLoadContext* ctx, const char *name);
static void rb_podcast_parser_characters (struct RBPoadcastLoadContext* ctx, const char *data, guint len);
static void rb_set_channel_value (struct RBPoadcastLoadContext* ctx, const char* name, const char* value);
static void rb_set_item_value (struct RBPoadcastLoadContext* ctx, const char* name, const char* value);

static RBPodcastItem *
rb_podcast_initializa_item ()
{
    RBPodcastItem *data = g_new0 (RBPodcastItem, 1);
    return data;
}

static void
rb_set_channel_value (struct RBPoadcastLoadContext* ctx, const char* name, const char* value)
{
   xmlChar *dvalue;
   if (!value)
       return;

   dvalue = xmlCharStrdup (value);
   g_strstrip ((char *)dvalue);
   
   if (!strcmp (name, "title")) {
        ctx->channel_data->title = dvalue;
   } else if (!strcmp (name, "language")) { 
        ctx->channel_data->lang = dvalue;
   } else if (!strcmp (name, "itunes:subtitle")) {
        ctx->channel_data->subtitle = dvalue;
   } else if (!strcmp (name, "itunes:summary")) { 
        ctx->channel_data->summary = dvalue;
   } else if (!strcmp (name, "description")) {
        ctx->channel_data->description = dvalue;
   } else if (!strcmp (name, "generator") ||
              !strcmp (name, "itunes:author")) {
        ctx->channel_data->author = dvalue;
   } else if (!strcmp (name, "webMaster")) {
        ctx->channel_data->contact = dvalue;
   } else if (!strcmp (name, "pubDate")) {
        ctx->channel_data->pub_date = rb_podcast_parse_date ((char *)dvalue);
        g_free (dvalue);
   } else if (!strcmp (name, "copyright")) {
        ctx->channel_data->copyright = dvalue;
   } else if (!strcmp (name, "img")) {
        ctx->channel_data->img = dvalue;
   } else {
	g_free (dvalue);
   }
}


static void
rb_set_item_value (struct RBPoadcastLoadContext* ctx, const char* name, const char* value)
{
   xmlChar *dvalue;
   dvalue = xmlCharStrdup (value);
   g_strstrip ((char *)dvalue);

   if (!strcmp (name, "title")) {
       ctx->item_data->title = dvalue;
   } else if (!strcmp (name, "url")) {
       ctx->item_data->url = dvalue;
   } else if (!strcmp (name, "pubDate")) {
       ctx->item_data->pub_date = rb_podcast_parse_date ((char *)dvalue);
       g_free (dvalue);
   } else if (!strcmp (name, "description")) {
       ctx->item_data->description = dvalue;
   } else if (!strcmp (name, "author")) {
       ctx->item_data->author = dvalue;
   } else if (!strcmp (name, "itunes:duration")) {
       ctx->item_data->duration = rb_podcast_parse_time ((char*)dvalue);
       g_free (dvalue);
   } else if (!strcmp (name, "length")) {
       ctx->item_data->filesize = g_ascii_strtoull ((char*)dvalue, NULL, 10);
   } else {
       g_free (dvalue);
   }
}


static void
rb_insert_item (struct RBPoadcastLoadContext* ctx)
{
	RBPodcastItem *data = ctx->item_data;

	if (!data->url)
		return;

	ctx->channel_data->posts = g_list_prepend (ctx->channel_data->posts, (void *) ctx->item_data);
}

static gboolean rb_validate_channel_propert (const char *name)
{
    if (!strcmp(name, "title") || 
        !strcmp(name, "language") ||
        !strcmp(name, "itunes:subtitle") ||
        !strcmp(name, "itunes:summary") ||
        !strcmp(name, "description") || 
        !strcmp(name, "generator") ||
        !strcmp(name, "itunes:author") ||
        !strcmp(name, "webMaster") ||
        !strcmp(name, "lastBuildDate") ||
        !strcmp(name, "pubDate") ||
        !strcmp(name, "copyright"))
        return TRUE;
    else
        return FALSE;
    
}

static gboolean rb_validate_item_propert (const char *name)
{
    if (!strcmp(name, "title") ||
        !strcmp(name, "url") ||
        !strcmp(name, "pubDate") ||
	!strcmp(name, "description") ||
	!strcmp(name, "author") ||
	!strcmp(name, "itunes:duration") )
	    
        return TRUE;
    else
        return FALSE;
}


static void 
rb_podcast_parser_start_element (struct RBPoadcastLoadContext* ctx, const char *name, const char **attrs)
{

    switch (ctx->state)
    {
        case RB_PODCAST_PARSER_STATE_START:
        {
            if (!strcmp(name, "rss")) {
                ctx->state = RB_PODCAST_PARSER_STATE_RSS;
            }
            else
                ctx->in_unknown_elt++;
            break;
        }
        
        case RB_PODCAST_PARSER_STATE_RSS:
        {
            if (!strcmp(name, "channel")) {
                ctx->state = RB_PODCAST_PARSER_STATE_CHANNEL;
            }
            else
                ctx->in_unknown_elt++;
            break;
        }

        case RB_PODCAST_PARSER_STATE_CHANNEL:
        {
 
            if (!strcmp(name, "image")) {
                ctx->state = RB_PODCAST_PARSER_STATE_IMG;
                break;
            } else  if (!strcmp(name, "item")) {
                ctx->item_data = rb_podcast_initializa_item(); // g_new0(RBPodcastItem, 1);
                ctx->state = RB_PODCAST_PARSER_STATE_ITEM;
                break;
            } else  if (!rb_validate_channel_propert (name)) {
                ctx->in_unknown_elt++;
                break;
            } 

            ctx->state = RB_PODCAST_PARSER_STATE_CHANNEL_PROPERTY;
            break;
        }

        case RB_PODCAST_PARSER_STATE_ITEM:
        {
            if (!strcmp(name, "enclosure")) {
                for (; *attrs; attrs +=2) {
                    if (!strcmp (*attrs, "url")) {
                        const char *url_value = *(attrs+1);
                        rb_set_item_value(ctx, "url", url_value);
                    } else if (!strcmp (*attrs, "length")) {
                        const char *length_value = *(attrs+1);
                        rb_set_item_value(ctx, "length", length_value);
                    }
                }
            } else  if (!rb_validate_item_propert (name)) {
                ctx->in_unknown_elt++;
                break;
            }
            

            ctx->state = RB_PODCAST_PARSER_STATE_ITEM_PROPERTY;
            break;
        }

        case RB_PODCAST_PARSER_STATE_IMG:
        {
            if (strcmp(name, "url") != 0) {
                ctx->in_unknown_elt++;
                break;
            }

            ctx->state = RB_PODCAST_PARSER_STATE_IMG_PROPERTY;
            break;
        }

        case RB_PODCAST_PARSER_STATE_CHANNEL_PROPERTY:
        case RB_PODCAST_PARSER_STATE_ITEM_PROPERTY:
        case RB_PODCAST_PARSER_STATE_IMG_PROPERTY:
        case RB_PODCAST_PARSER_STATE_END:
        break;
    }
}


static void
rb_podcast_parser_end_element (struct RBPoadcastLoadContext* ctx, 
                                const char *name)
{
//    if (*ctx->die == TRUE) {
//        xmlStopParser (ctx->xmlctx);
//        return;
//    }

    if (ctx->in_unknown_elt > 0) {
        ctx->in_unknown_elt--;
        return;
    }
               
    switch (ctx->state)
    {
        case RB_PODCAST_PARSER_STATE_START:
            ctx->state = RB_PODCAST_PARSER_STATE_END;
            break;
            
        case RB_PODCAST_PARSER_STATE_RSS:
            ctx->state = RB_PODCAST_PARSER_STATE_START;
            break;

        case RB_PODCAST_PARSER_STATE_CHANNEL:
            ctx->state = RB_PODCAST_PARSER_STATE_RSS;
            break;
            
        case RB_PODCAST_PARSER_STATE_CHANNEL_PROPERTY:
        {
            rb_set_channel_value(ctx, name, ctx->prop_value->str);
            ctx->state = RB_PODCAST_PARSER_STATE_CHANNEL;
            g_string_truncate (ctx->prop_value, 0);
            break;
        }

        case RB_PODCAST_PARSER_STATE_ITEM:
        {
            rb_insert_item(ctx);
            ctx->state = RB_PODCAST_PARSER_STATE_CHANNEL;
            break;
        }

        case RB_PODCAST_PARSER_STATE_ITEM_PROPERTY:
        {
            rb_set_item_value(ctx, name, ctx->prop_value->str);
            ctx->state = RB_PODCAST_PARSER_STATE_ITEM;
            g_string_truncate (ctx->prop_value, 0);
            break;
        }

        case RB_PODCAST_PARSER_STATE_IMG_PROPERTY:
        {
            rb_set_channel_value(ctx, "img", ctx->prop_value->str);
            ctx->state = RB_PODCAST_PARSER_STATE_IMG;
            g_string_truncate (ctx->prop_value, 0);
            break;
        }

        case RB_PODCAST_PARSER_STATE_IMG:
            ctx->state = RB_PODCAST_PARSER_STATE_CHANNEL;
            break;

        case RB_PODCAST_PARSER_STATE_END:
            break;
    }
}
    

static void
rb_podcast_parser_characters (struct RBPoadcastLoadContext* ctx, const char *data,
                                     guint len)
{
    switch (ctx->state)
    {
        case RB_PODCAST_PARSER_STATE_CHANNEL_PROPERTY:
        case RB_PODCAST_PARSER_STATE_ITEM_PROPERTY:
        case RB_PODCAST_PARSER_STATE_IMG_PROPERTY:            
		g_string_append_len (ctx->prop_value, data, len);
           	break;
        case RB_PODCAST_PARSER_STATE_START:
        case RB_PODCAST_PARSER_STATE_IMG:
        case RB_PODCAST_PARSER_STATE_RSS:
        case RB_PODCAST_PARSER_STATE_CHANNEL:
        case RB_PODCAST_PARSER_STATE_ITEM:
        case RB_PODCAST_PARSER_STATE_END:
            break;
    }
}


void
rb_podcast_parse_load_feed(RBPodcastChannel *data, const char *file_name) {

    xmlParserCtxtPtr ctxt;
    xmlSAXHandlerPtr sax_handler = g_new0 (xmlSAXHandler, 1);
    GnomeVFSResult result;
    GnomeVFSFileInfo *info;
    gint file_size;
    gchar *buffer;
    
    struct RBPoadcastLoadContext *ctx = g_new0 (struct RBPoadcastLoadContext, 1);

    data->url = xmlCharStrdup (file_name);
    
    ctx->in_unknown_elt = 0;
    ctx->channel_data = data;

    if (!gnome_vfs_initialized ()) {
        goto end_function;
    }

    if (!g_str_has_suffix (file_name, ".rss") && !g_str_has_suffix (file_name, ".xml")) {
        info = gnome_vfs_file_info_new();

        result = gnome_vfs_get_file_info (file_name, info, GNOME_VFS_FILE_INFO_DEFAULT);

        if ((result != GNOME_VFS_OK) || 
	    (info->mime_type == NULL) || 
	     ((strstr (info->mime_type, "xml") == NULL) && 
	      (strstr (info->mime_type, "rss") == NULL))) {
	    rb_debug ("Invalid mime-type in podcast feed %s", info->mime_type);
	    gnome_vfs_file_info_unref (info);
	    return;
	}

        gnome_vfs_file_info_unref (info);
    }
    
    /* first download file by gnome_vfs for use gnome network configuration */
    result = gnome_vfs_read_entire_file (file_name, &file_size, &buffer);
    if (result != GNOME_VFS_OK)
	    return;
   
	    

    //initializing parse 
    sax_handler->startElement = (startElementSAXFunc) rb_podcast_parser_start_element;
    sax_handler->endElement = (endElementSAXFunc) rb_podcast_parser_end_element;
    sax_handler->characters = (charactersSAXFunc) rb_podcast_parser_characters;
    xmlSubstituteEntitiesDefault (1);

    ctx->prop_value = g_string_sized_new(512);
                            
    ctxt = xmlCreateMemoryParserCtxt (buffer, file_size);
    ctx->xmlctx = ctxt;
    xmlFree (ctxt->sax);
    ctxt->userData = ctx;
    ctxt->sax = sax_handler;
    xmlParseDocument (ctxt);
    ctxt->sax = NULL;
    xmlFreeParserCtxt (ctxt);

    g_free (buffer);
    g_string_free(ctx->prop_value, TRUE);
    ctx->channel_data->posts = g_list_reverse (ctx->channel_data->posts);

end_function:
    g_free(sax_handler);    
    g_free(ctx);
}

static uintmax_t
rb_podcast_parse_date(const char* date_str)
{
	struct tm tm;
	char *result;

	result = strptime (date_str, "%a, %d %b %Y %T", &tm);
	if (result == NULL) {
		memset (&tm, 0, sizeof (struct tm));
		result = strptime (date_str, "%d %b %Y %T", &tm);
	}
	if (result == NULL) {
		memset (&tm, 0, sizeof (struct tm));	
		rb_debug ("unable to convert date string %s", date_str);
	}

	return (uintmax_t) mktime (&tm);
}

static gulong
rb_podcast_parse_time (const char *time_str)
{
	struct tm tm;
	char *result;

	memset (&tm, 0, sizeof (struct tm));
	result = strptime (time_str, "%H:%M:%S", &tm);
	if (result == NULL) {
		memset (&tm, 0, sizeof (struct tm));
		result = strptime (time_str, "%M:%S", &tm);
	}
	if (result == NULL) {
		memset (&tm, 0, sizeof (struct tm));	
		rb_debug ("unable to convert duration string %s", time_str);
	}
	
	return ((tm.tm_hour * 60 + tm.tm_min) * 60 + tm.tm_sec);
}

void
rb_podcast_parse_channel_free (RBPodcastChannel *data)
{
	g_return_if_fail (data != NULL);
	
	g_list_foreach (data->posts, (GFunc) rb_podcast_parse_item_free, NULL);
	g_list_free (data->posts);

	if (data->url != NULL)
		g_free (data->url);

	if (data->title != NULL)
		g_free (data->title);

	if (data->lang != NULL)
		g_free (data->lang);

	if (data->subtitle != NULL)
		g_free (data->subtitle);

	if (data->summary != NULL)
		g_free (data->summary);

	if (data->description != NULL)
		g_free (data->description);

	if (data->author != NULL)
		g_free (data->author);

	if (data->contact != NULL)
		g_free (data->contact);

	if (data->img != NULL)
		g_free (data->img);

	if (data->copyright != NULL)
		g_free (data->copyright);
	
	g_free (data);
}
		
void 
rb_podcast_parse_item_free (RBPodcastItem *item)
{
	g_return_if_fail (item != NULL);
	
	if (item->title != NULL)
		g_free (item->title);

	if (item->url != NULL)
		g_free (item->url);

	if (item->description != NULL)
		g_free (item->description);

	g_free (item);
}
