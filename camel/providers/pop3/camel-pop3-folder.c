/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-pop3-folder.c : class for a pop3 folder */

/*
 * Authors:
 *   Dan Winship <danw@ximian.com>
 *   Michael Zucchi <notzed@ximian.com>
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301
 * USA
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <glib/gi18n-lib.h>

#include "camel-pop3-folder.h"
#include "camel-pop3-store.h"

#define d(x) if (camel_debug("pop")) x;
#define FIRST_TIME_DOWNLOAD 40

static gboolean pop3_refresh_info (CamelFolder *folder, GError **error);
static gboolean pop3_sync (CamelFolder *folder, gboolean expunge, GError **error);
static gint pop3_get_message_count (CamelFolder *folder);
static GPtrArray *pop3_get_uids (CamelFolder *folder);
static CamelMimeMessage *pop3_get_message (CamelFolder *folder, const gchar *uid, GError **error);
static gboolean pop3_set_message_flags (CamelFolder *folder, const gchar *uid, guint32 flags, guint32 set);
static gchar * pop3_get_filename (CamelFolder *folder, const gchar *uid, GError **error);
static gboolean pop3_fetch_old_messages (CamelFolder *folder, int count, GError **error);

G_DEFINE_TYPE (CamelPOP3Folder, camel_pop3_folder, CAMEL_TYPE_FOLDER)

static void
pop3_folder_dispose (GObject *object)
{
	CamelPOP3Folder *pop3_folder = CAMEL_POP3_FOLDER (object);
	CamelPOP3FolderInfo **fi = (CamelPOP3FolderInfo **)pop3_folder->uids->pdata;
	CamelPOP3Store *pop3_store;
	CamelStore *parent_store;
	gint i;

	parent_store = camel_folder_get_parent_store (CAMEL_FOLDER (object));
	if (parent_store) {
		pop3_store = CAMEL_POP3_STORE (parent_store);

		if (pop3_folder->uids) {
			for (i = 0; i < pop3_folder->uids->len; i++, fi++) {
				if (fi[0]->cmd) {
					while (camel_pop3_engine_iterate (pop3_store->engine, fi[0]->cmd) > 0)
						;
					camel_pop3_engine_command_free (pop3_store->engine, fi[0]->cmd);
				}

				g_free (fi[0]->uid);
				g_free (fi[0]);
			}

			g_ptr_array_free (pop3_folder->uids, TRUE);
			g_hash_table_destroy (pop3_folder->uids_uid);
		}
	}

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (camel_pop3_folder_parent_class)->dispose (object);
}

static void
camel_pop3_folder_class_init (CamelPOP3FolderClass *class)
{
	GObjectClass *object_class;
	CamelFolderClass *folder_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->dispose = pop3_folder_dispose;

	folder_class = CAMEL_FOLDER_CLASS (class);
	folder_class->refresh_info = pop3_refresh_info;
	folder_class->sync = pop3_sync;
	folder_class->get_message_count = pop3_get_message_count;
	folder_class->get_uids = pop3_get_uids;
	folder_class->free_uids = camel_folder_free_shallow;
	folder_class->get_filename = pop3_get_filename;
	folder_class->get_message = pop3_get_message;
	folder_class->fetch_old_messages = pop3_fetch_old_messages;
	folder_class->set_message_flags = pop3_set_message_flags;
}

static void
camel_pop3_folder_init (CamelPOP3Folder *pop3_folder)
{
}

CamelFolder *
camel_pop3_folder_new (CamelStore *parent, GError **error)
{
	CamelFolder *folder;
	CamelURL *url;
	CamelPOP3Folder *pop3_folder;
	CamelService *service;

	d(printf("opening pop3 INBOX folder\n"));

	folder = g_object_new (
		CAMEL_TYPE_POP3_FOLDER,
		"full-name", "inbox", "name", "inbox",
		"parent-store", parent, NULL);

	pop3_folder = (CamelPOP3Folder *)folder;
	service = (CamelService *) parent;

	/* This decides if the POP3 downloads entire content or a limited content. */
	url = service->url;
	if (camel_url_get_param(url, "mobile"))
		pop3_folder->mobile = TRUE;
	else
		pop3_folder->mobile = FALSE;
	pop3_folder->fetch_more = 0;

	if (pop3_folder->mobile) {
		/* Setup Keyfile */
		char *path, *root;

		pop3_folder->key_file = g_key_file_new();
		root = camel_session_get_storage_path (camel_service_get_session(service), service, NULL);
		path = g_build_filename (root, "uidconfig", NULL);
		g_key_file_load_from_file (pop3_folder->key_file, path, G_KEY_FILE_NONE, NULL);

		g_free (path);
		g_free(root);
		
	}
	/* mt-ok, since we dont have the folder-lock for new() */
	if (!camel_folder_refresh_info (folder, error)) { /* mt-ok */
		g_object_unref (folder);
		folder = NULL;
	}

	return folder;
}

/* create a uid from md5 of 'top' output */
static void
cmd_builduid(CamelPOP3Engine *pe, CamelPOP3Stream *stream, gpointer data)
{
	GChecksum *checksum;
	CamelPOP3FolderInfo *fi = data;
	struct _camel_header_raw *h;
	CamelMimeParser *mp;
	guint8 *digest;
	gsize length;

	length = g_checksum_type_get_length (G_CHECKSUM_MD5);
	digest = g_alloca (length);

	/* TODO; somehow work out the limit and use that for proper progress reporting
	   We need a pointer to the folder perhaps? */
	camel_operation_progress (NULL, fi->id);

	checksum = g_checksum_new (G_CHECKSUM_MD5);
	mp = camel_mime_parser_new();
	camel_mime_parser_init_with_stream(mp, (CamelStream *)stream, NULL);
	switch (camel_mime_parser_step(mp, NULL, NULL)) {
	case CAMEL_MIME_PARSER_STATE_HEADER:
	case CAMEL_MIME_PARSER_STATE_MESSAGE:
	case CAMEL_MIME_PARSER_STATE_MULTIPART:
		h = camel_mime_parser_headers_raw(mp);
		while (h) {
			if (g_ascii_strcasecmp(h->name, "status") != 0
			    && g_ascii_strcasecmp(h->name, "x-status") != 0) {
				g_checksum_update (checksum, (guchar *) h->name, -1);
				g_checksum_update (checksum, (guchar *) h->value, -1);
			}
			h = h->next;
		}
	default:
		break;
	}
	g_object_unref (mp);
	g_checksum_get_digest (checksum, digest, &length);
	g_checksum_free (checksum);

	fi->uid = g_base64_encode ((guchar *) digest, length);

	d(printf("building uid for id '%d' = '%s'\n", fi->id, fi->uid));
}

static void
cmd_list(CamelPOP3Engine *pe, CamelPOP3Stream *stream, gpointer data)
{
	gint ret;
	guint len, id, size;
	guchar *line;
	CamelFolder *folder = data;
	CamelStore *parent_store;
	CamelPOP3Store *pop3_store;
	CamelPOP3FolderInfo *fi;
	int i=0, total, last_uid=-1;
	CamelPOP3Folder *pop3_folder;
	CamelService *service;

	parent_store = camel_folder_get_parent_store (folder);
	pop3_store = CAMEL_POP3_STORE (parent_store);
	pop3_folder = (CamelPOP3Folder *)folder;
	service = (CamelService *) parent_store;

	do {
		ret = camel_pop3_stream_line(stream, &line, &len);
		if (ret>=0) {
			if (sscanf((gchar *) line, "%u %u", &id, &size) == 2) {
				fi = g_malloc0(sizeof(*fi));
				fi->size = size;
				fi->id = id;
				fi->index = ((CamelPOP3Folder *)folder)->uids->len;
				if ((pop3_store->engine->capa & CAMEL_POP3_CAP_UIDL) == 0)
					fi->cmd = camel_pop3_engine_command_new(pe, CAMEL_POP3_COMMAND_MULTI, cmd_builduid, fi, "TOP %u 0\r\n", id);
				g_ptr_array_add(pop3_folder->uids, fi);
				g_hash_table_insert(pop3_folder->uids_id, GINT_TO_POINTER(id), fi);
			}
		}
	} while (ret>0);

	/* Trim the list for mobile devices*/
	if (pop3_folder->mobile) {
		gboolean save_uid = FALSE;
		
		d(printf("*********** Mobile mode *************\n"));
		d(printf("Total Count: %s: %d\n", service->url->host, pop3_folder->uids->len));

		/* Preserve the first message's ID */
		fi = pop3_folder->uids->pdata[0];
		pop3_folder->first_id = fi->id;

		total = pop3_folder->uids->len;
		if (pop3_folder->key_file) {
			last_uid = g_key_file_get_integer (pop3_folder->key_file, "UIDConfig", "last-saved-uid", NULL);
			if (!last_uid) {
				/* First time downloading the POP folder, lets just download only a batch. */
				last_uid = -1;
			}
			d(printf("Last stored' first uid: %d\n", last_uid));
		}

		if (last_uid == -1)
			save_uid = TRUE;

		for (i=total-1; i >= 0; i--) {
			fi = pop3_folder->uids->pdata[i];

			if ((last_uid != -1 && last_uid >= fi->id) || (last_uid == -1 && i == total-FIRST_TIME_DOWNLOAD)) {
				if (last_uid != -1 && last_uid < fi->id)
					i++; /* if the last uid was deleted on the server, then we need the last but 1*/

				
				break;
			} 
			
		}
		if (i> 0 && pop3_folder->fetch_more) {
			int k=0;
			/* Lets pull another window of old messages */
			save_uid = TRUE;
			/* Decrement 'i' by another batch count or till we reach the first message */
			d(printf("Fetch more (%d): from %d", pop3_folder->fetch_more, i));
			for (k=0; k< pop3_folder->fetch_more && i>= 0; k++, i--);
			d(printf(" to %d\n", i));
				
		}
		
		if (i > 0) {
			int j=0;
			/* i is the start of the last fetch UID, so remove everything else from 0 to i */
			for (; j<i; j++) {
				fi = pop3_folder->uids->pdata[j];
				g_hash_table_remove (pop3_folder->uids_id, GINT_TO_POINTER(fi->id));
			}
			g_ptr_array_remove_range (pop3_folder->uids, 0, i);
			d(printf("Removing %d uids that are old\n", i));
			
		} 

		if (save_uid) {
			char *contents;
			gsize len;
			char *root, *path;
	
			/* Save the last fetched UID */
			fi = pop3_folder->uids->pdata[0];			
			g_key_file_set_integer (pop3_folder->key_file, "UIDConfig", "last-saved-uid", fi->id);
			contents = g_key_file_to_data (pop3_folder->key_file, &len, NULL);
			root = camel_session_get_storage_path (camel_service_get_session(service), service, NULL);
			path = g_build_filename (root, "uidconfig", NULL);
			g_file_set_contents (path, contents, len, NULL);
			g_key_file_load_from_file (pop3_folder->key_file, path, G_KEY_FILE_NONE, NULL);		
			g_free (contents);
			g_free (path);
			g_free(root);
			d(printf("Saving last uid %d\n", fi->id));
			
		}

	}

}	

static void
cmd_uidl(CamelPOP3Engine *pe, CamelPOP3Stream *stream, gpointer data)
{
	gint ret;
	guint len;
	guchar *line;
	gchar uid[1025];
	guint id;
	CamelPOP3FolderInfo *fi;
	CamelPOP3Folder *folder = data;

	do {
		ret = camel_pop3_stream_line(stream, &line, &len);
		if (ret>=0) {
			if (strlen((gchar *) line) > 1024)
				line[1024] = 0;
			if (sscanf((gchar *) line, "%u %s", &id, uid) == 2) {
				fi = g_hash_table_lookup(folder->uids_id, GINT_TO_POINTER(id));
				if (fi) {
					camel_operation_progress(NULL, (fi->index+1) * 100 / folder->uids->len);
					fi->uid = g_strdup(uid);
					g_hash_table_insert(folder->uids_uid, fi->uid, fi);
				} else {
					//g_warning("ID %u (uid: %s) not in previous LIST output", id, uid);
				}
			}
		}
	} while (ret>0);
}

static gboolean
pop3_refresh_info (CamelFolder *folder, GError **error)
{
	CamelStore *parent_store;
	CamelPOP3Store *pop3_store;
	CamelPOP3Folder *pop3_folder = (CamelPOP3Folder *) folder;
	CamelPOP3Command *pcl, *pcu = NULL;
	gboolean success = TRUE;
	gint i;

	parent_store = camel_folder_get_parent_store (folder);
	pop3_store = CAMEL_POP3_STORE (parent_store);

	camel_operation_start (NULL, _("Retrieving POP summary"));

	pop3_folder->uids = g_ptr_array_new ();
	pop3_folder->uids_uid = g_hash_table_new(g_str_hash, g_str_equal);
	/* only used during setup */
	pop3_folder->uids_id = g_hash_table_new(NULL, NULL);

	pcl = camel_pop3_engine_command_new(pop3_store->engine, CAMEL_POP3_COMMAND_MULTI, cmd_list, folder, "LIST\r\n");
	if (pop3_store->engine->capa & CAMEL_POP3_CAP_UIDL)
		pcu = camel_pop3_engine_command_new(pop3_store->engine, CAMEL_POP3_COMMAND_MULTI, cmd_uidl, folder, "UIDL\r\n");
	while ((i = camel_pop3_engine_iterate(pop3_store->engine, NULL)) > 0)
		;

	if (i == -1) {
		if (errno == EINTR)
			g_set_error (
				error, G_IO_ERROR,
				G_IO_ERROR_CANCELLED,
				_("Cancelled"));
		else
			g_set_error (
				error, G_IO_ERROR,
				g_io_error_from_errno (errno),
				_("Cannot get POP summary: %s"),
				g_strerror (errno));
		success = FALSE;
	}

	/* TODO: check every id has a uid & commands returned OK too? */

	camel_pop3_engine_command_free(pop3_store->engine, pcl);

	if (pop3_store->engine->capa & CAMEL_POP3_CAP_UIDL) {
		camel_pop3_engine_command_free(pop3_store->engine, pcu);
	} else {
		for (i=0;i<pop3_folder->uids->len;i++) {
			CamelPOP3FolderInfo *fi = pop3_folder->uids->pdata[i];
			if (fi->cmd) {
				camel_pop3_engine_command_free(pop3_store->engine, fi->cmd);
				fi->cmd = NULL;
			}
			if (fi->uid)
				g_hash_table_insert(pop3_folder->uids_uid, fi->uid, fi);
		}
	}

	/* dont need this anymore */
	g_hash_table_destroy(pop3_folder->uids_id);

	camel_operation_end (NULL);

	return success;
}

static gboolean
pop3_fetch_old_messages (CamelFolder *folder, int count, GError **error)
{
	CamelPOP3FolderInfo *fi;
	CamelPOP3Folder *pop3_folder = (CamelPOP3Folder *)folder;

	/* If we have the first message already, then return FALSE */
	fi = pop3_folder->uids->pdata[0];
	if (fi->id == pop3_folder->first_id)
		return FALSE;
	
	pop3_folder->fetch_more = count;
	pop3_refresh_info (folder, error);
	pop3_folder->fetch_more = 0;

	/* Even if we downloaded the first/oldest message, just now, return TRUE so that we wont waste another cycle */
	fi = pop3_folder->uids->pdata[0];
	if (fi->id == pop3_folder->first_id)
		return FALSE;

	return TRUE;
}

static gboolean
pop3_sync (CamelFolder *folder,
           gboolean expunge,
           GError **error)
{
	CamelStore *parent_store;
	CamelPOP3Folder *pop3_folder;
	CamelPOP3Store *pop3_store;
	gint i;
	CamelPOP3FolderInfo *fi;

	parent_store = camel_folder_get_parent_store (folder);

	pop3_folder = CAMEL_POP3_FOLDER (folder);
	pop3_store = CAMEL_POP3_STORE (parent_store);

	if (pop3_store->delete_after && !expunge) {
		d(printf("%s(%d): pop3_store->delete_after = [%d], expunge=[%d]\n",
			 __FILE__, __LINE__, pop3_store->delete_after, expunge));
		camel_operation_start(NULL, _("Expunging old messages"));
		camel_pop3_delete_old(folder, pop3_store->delete_after, error);
	}

	if (!expunge) {
		return TRUE;
	}

	camel_operation_start(NULL, _("Expunging deleted messages"));

	for (i = 0; i < pop3_folder->uids->len; i++) {
		fi = pop3_folder->uids->pdata[i];
		/* busy already?  wait for that to finish first */
		if (fi->cmd) {
			while (camel_pop3_engine_iterate(pop3_store->engine, fi->cmd) > 0)
				;
			camel_pop3_engine_command_free(pop3_store->engine, fi->cmd);
			fi->cmd = NULL;
		}

		if (fi->flags & CAMEL_MESSAGE_DELETED) {
			fi->cmd = camel_pop3_engine_command_new(pop3_store->engine,
								0,
								NULL,
								NULL,
								"DELE %u\r\n",
								fi->id);

			/* also remove from cache */
			if (pop3_store->cache && fi->uid)
				camel_data_cache_remove(pop3_store->cache, "cache", fi->uid, NULL);
		}
	}

	for (i = 0; i < pop3_folder->uids->len; i++) {
		fi = pop3_folder->uids->pdata[i];
		/* wait for delete commands to finish */
		if (fi->cmd) {
			while (camel_pop3_engine_iterate(pop3_store->engine, fi->cmd) > 0)
				;
			camel_pop3_engine_command_free(pop3_store->engine, fi->cmd);
			fi->cmd = NULL;
		}
		camel_operation_progress(NULL, (i+1) * 100 / pop3_folder->uids->len);
	}

	camel_operation_end(NULL);

	camel_pop3_store_expunge (pop3_store, error);

	return TRUE;
}

static gboolean
pop3_get_message_time_from_cache (CamelFolder *folder, const gchar *uid, time_t *message_time)
{
	CamelStore *parent_store;
	CamelPOP3Store *pop3_store;
	CamelStream *stream = NULL;
	gchar buffer[1];
	gboolean res = FALSE;

	g_return_val_if_fail (folder != NULL, FALSE);
	g_return_val_if_fail (uid != NULL, FALSE);
	g_return_val_if_fail (message_time != NULL, FALSE);

	parent_store = camel_folder_get_parent_store (folder);
	pop3_store = CAMEL_POP3_STORE (parent_store);

	g_return_val_if_fail (pop3_store->cache != NULL, FALSE);

	if ((stream = camel_data_cache_get (pop3_store->cache, "cache", uid, NULL)) != NULL
	    && camel_stream_read (stream, buffer, 1, NULL) == 1
	    && buffer[0] == '#') {
		CamelMimeMessage *message;

		message = camel_mime_message_new ();
		if (camel_data_wrapper_construct_from_stream ((CamelDataWrapper *)message, stream, NULL) == -1) {
			g_warning (_("Cannot get message %s: %s"), uid, g_strerror (errno));
			g_object_unref (message);
			message = NULL;
		}

		if (message) {
			res = TRUE;
			*message_time = message->date + message->date_offset;

			g_object_unref (message);
		}
	}

	if (stream) {
		g_object_unref (stream);
	}
	return res;
}

gint
camel_pop3_delete_old (CamelFolder *folder,
                       gint days_to_delete,
                       GError **error)
{
	CamelStore *parent_store;
	CamelPOP3Folder *pop3_folder;
	CamelPOP3FolderInfo *fi;
	gint i;
	CamelPOP3Store *pop3_store;
	CamelMimeMessage *message;
	time_t temp, message_time;

	parent_store = camel_folder_get_parent_store (folder);

	pop3_folder = CAMEL_POP3_FOLDER (folder);
	pop3_store = CAMEL_POP3_STORE (parent_store);
	temp = time(&temp);

	d(printf("%s(%d): pop3_folder->uids->len=[%d]\n", __FILE__, __LINE__, pop3_folder->uids->len));
	for (i = 0; i < pop3_folder->uids->len; i++) {
		message_time = 0;
		fi = pop3_folder->uids->pdata[i];

		d(printf("%s(%d): fi->uid=[%s]\n", __FILE__, __LINE__, fi->uid));
		if (!pop3_get_message_time_from_cache (folder, fi->uid, &message_time)) {
			d(printf("could not get message time from cache, trying from pop3\n"));
			message = pop3_get_message (folder, fi->uid, error);
			if (message) {
				message_time = message->date + message->date_offset;
				g_object_unref (message);
			}
		}

		if (message_time) {
			gdouble time_diff = difftime(temp,message_time);
			gint day_lag = time_diff/(60*60*24);

			d(printf("%s(%d): message_time= [%ld]\n", __FILE__, __LINE__, message_time));
			d(printf("%s(%d): day_lag=[%d] \t days_to_delete=[%d]\n",
				__FILE__, __LINE__, day_lag, days_to_delete));

			if (day_lag > days_to_delete) {
				if (fi->cmd) {
					while (camel_pop3_engine_iterate(pop3_store->engine, fi->cmd) > 0) {
						; /* do nothing - iterating until end */
					}

					camel_pop3_engine_command_free(pop3_store->engine, fi->cmd);
					fi->cmd = NULL;
				}
				d(printf("%s(%d): Deleting old messages\n", __FILE__, __LINE__));
				fi->cmd = camel_pop3_engine_command_new(pop3_store->engine,
									0,
									NULL,
									NULL,
									"DELE %u\r\n",
									fi->id);
				/* also remove from cache */
				if (pop3_store->cache && fi->uid) {
					camel_data_cache_remove(pop3_store->cache, "cache", fi->uid, NULL);
				}
			}
		}
	}

	for (i = 0; i < pop3_folder->uids->len; i++) {
		fi = pop3_folder->uids->pdata[i];
		/* wait for delete commands to finish */
		if (fi->cmd) {
			while (camel_pop3_engine_iterate(pop3_store->engine, fi->cmd) > 0)
				;
			camel_pop3_engine_command_free(pop3_store->engine, fi->cmd);
			fi->cmd = NULL;
		}
		camel_operation_progress(NULL, (i+1) * 100 / pop3_folder->uids->len);
	}

	camel_operation_end(NULL);

	camel_pop3_store_expunge (pop3_store, error);

	return 0;
}

static void
cmd_tocache(CamelPOP3Engine *pe, CamelPOP3Stream *stream, gpointer data)
{
	CamelPOP3FolderInfo *fi = data;
	gchar buffer[2048];
	gint w = 0, n;

	/* What if it fails? */

	/* We write an '*' to the start of the stream to say its not complete yet */
	/* This should probably be part of the cache code */
	if ((n = camel_stream_write (fi->stream, "*", 1, NULL)) == -1)
		goto done;

	while ((n = camel_stream_read((CamelStream *)stream, buffer, sizeof(buffer), NULL)) > 0) {
		n = camel_stream_write(fi->stream, buffer, n, NULL);
		if (n == -1)
			break;

		w += n;
		if (w > fi->size)
			w = fi->size;
		if (fi->size != 0)
			camel_operation_progress(NULL, (w * 100) / fi->size);
	}

	/* it all worked, output a '#' to say we're a-ok */
	if (n != -1) {
		camel_stream_reset(fi->stream, NULL);
		n = camel_stream_write(fi->stream, "#", 1, NULL);
	}
done:
	if (n == -1) {
		fi->err = errno;
		g_warning("POP3 retrieval failed: %s", g_strerror(errno));
	} else {
		fi->err = 0;
	}

	g_object_unref (fi->stream);
	fi->stream = NULL;
}

static gchar *
pop3_get_filename (CamelFolder *folder, const gchar *uid, GError **error)
{
	CamelStore *parent_store;
	CamelPOP3Folder *pop3_folder;
	CamelPOP3Store *pop3_store;
	CamelPOP3FolderInfo *fi;

	parent_store = camel_folder_get_parent_store (folder);

	pop3_folder = CAMEL_POP3_FOLDER (folder);
	pop3_store = CAMEL_POP3_STORE (parent_store);

	fi = g_hash_table_lookup(pop3_folder->uids_uid, uid);
	if (fi == NULL) {
		g_set_error (
			error, CAMEL_FOLDER_ERROR,
			CAMEL_FOLDER_ERROR_INVALID_UID,
			_("No message with UID %s"), uid);
		return NULL;
	}

	return camel_data_cache_get_filename (pop3_store->cache, "cache", fi->uid, NULL);
}

static CamelMimeMessage *
pop3_get_message (CamelFolder *folder, const gchar *uid, GError **error)
{
	CamelStore *parent_store;
	CamelMimeMessage *message = NULL;
	CamelPOP3Store *pop3_store;
	CamelPOP3Folder *pop3_folder;
	CamelPOP3Command *pcr;
	CamelPOP3FolderInfo *fi;
	gchar buffer[1];
	gint i, last;
	CamelStream *stream = NULL;

	parent_store = camel_folder_get_parent_store (folder);

	pop3_folder = CAMEL_POP3_FOLDER (folder);
	pop3_store = CAMEL_POP3_STORE (parent_store);

	fi = g_hash_table_lookup(pop3_folder->uids_uid, uid);
	if (fi == NULL) {
		g_set_error (
			error, CAMEL_FOLDER_ERROR,
			CAMEL_FOLDER_ERROR_INVALID_UID,
			_("No message with UID %s"), uid);
		return NULL;
	}

	/* Sigh, most of the crap in this function is so that the cancel button
	   returns the proper exception code.  Sigh. */

	camel_operation_start_transient(NULL, _("Retrieving POP message %d"), fi->id);

	/* If we have an oustanding retrieve message running, wait for that to complete
	   & then retrieve from cache, otherwise, start a new one, and similar */

	if (fi->cmd != NULL) {
		while ((i = camel_pop3_engine_iterate(pop3_store->engine, fi->cmd)) > 0)
			;

		if (i == -1)
			fi->err = errno;

		/* getting error code? */
		/*g_assert (fi->cmd->state == CAMEL_POP3_COMMAND_DATA);*/
		camel_pop3_engine_command_free(pop3_store->engine, fi->cmd);
		fi->cmd = NULL;

		if (fi->err != 0) {
			if (fi->err == EINTR)
				g_set_error (
					error, G_IO_ERROR,
					G_IO_ERROR_CANCELLED,
					_("Cancelled"));
			else
				g_set_error (
					error, CAMEL_ERROR,
					CAMEL_ERROR_GENERIC,
					_("Cannot get message %s: %s"),
					uid, g_strerror (fi->err));
			goto fail;
		}
	}

	/* check to see if we have safely written flag set */
	if (pop3_store->cache == NULL
	    || (stream = camel_data_cache_get(pop3_store->cache, "cache", fi->uid, NULL)) == NULL
	    || camel_stream_read(stream, buffer, 1, NULL) != 1
	    || buffer[0] != '#') {

		/* Initiate retrieval, if disk backing fails, use a memory backing */
		if (pop3_store->cache == NULL
		    || (stream = camel_data_cache_add(pop3_store->cache, "cache", fi->uid, NULL)) == NULL)
			stream = camel_stream_mem_new();

		/* ref it, the cache storage routine unref's when done */
		fi->stream = g_object_ref (stream);
		fi->err = EIO;
		pcr = camel_pop3_engine_command_new(pop3_store->engine, CAMEL_POP3_COMMAND_MULTI, cmd_tocache, fi, "RETR %u\r\n", fi->id);

		/* Also initiate retrieval of some of the following messages, assume we'll be receiving them */
		if (pop3_store->auto_fetch && pop3_store->cache != NULL) {
			/* This should keep track of the last one retrieved, also how many are still
			   oustanding incase of random access on large folders */
			i = fi->index+1;
			last = MIN(i+10, pop3_folder->uids->len);
			for (;i<last;i++) {
				CamelPOP3FolderInfo *pfi = pop3_folder->uids->pdata[i];

				if (pfi->uid && pfi->cmd == NULL) {
					pfi->stream = camel_data_cache_add(pop3_store->cache, "cache", pfi->uid, NULL);
					if (pfi->stream) {
						pfi->err = EIO;
						pfi->cmd = camel_pop3_engine_command_new(pop3_store->engine, CAMEL_POP3_COMMAND_MULTI,
											 cmd_tocache, pfi, "RETR %u\r\n", pfi->id);
					}
				}
			}
		}

		/* now wait for the first one to finish */
		while ((i = camel_pop3_engine_iterate(pop3_store->engine, pcr)) > 0)
			;

		if (i == -1)
			fi->err = errno;

		/* getting error code? */
		/*g_assert (pcr->state == CAMEL_POP3_COMMAND_DATA);*/
		camel_pop3_engine_command_free(pop3_store->engine, pcr);
		camel_stream_reset(stream, NULL);

		/* Check to see we have safely written flag set */
		if (fi->err != 0) {
			if (fi->err == EINTR)
				g_set_error (
					error, G_IO_ERROR,
					G_IO_ERROR_CANCELLED,
					_("Cancelled"));
			else
				g_set_error (
					error, CAMEL_ERROR,
					CAMEL_ERROR_GENERIC,
					_("Cannot get message %s: %s"),
					uid, g_strerror (fi->err));
			goto done;
		}

		if (camel_stream_read (stream, buffer, 1, error) == -1)
			goto done;

		if (buffer[0] != '#') {
			g_set_error (
				error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
				_("Cannot get message %s: %s"), uid,
				_("Unknown reason"));
			goto done;
		}
	}

	message = camel_mime_message_new ();
	if (camel_data_wrapper_construct_from_stream((CamelDataWrapper *)message, stream, error) == -1) {
		g_prefix_error (error, _("Cannot get message %s: "), uid);
		g_object_unref (message);
		message = NULL;
	}
done:
	g_object_unref (stream);
fail:
	camel_operation_end(NULL);

	return message;
}

static gboolean
pop3_set_message_flags (CamelFolder *folder, const gchar *uid, guint32 flags, guint32 set)
{
	CamelPOP3Folder *pop3_folder = CAMEL_POP3_FOLDER (folder);
	CamelPOP3FolderInfo *fi;
	gboolean res = FALSE;

	fi = g_hash_table_lookup(pop3_folder->uids_uid, uid);
	if (fi) {
		guint32 new = (fi->flags & ~flags) | (set & flags);

		if (fi->flags != new) {
			fi->flags = new;
			res = TRUE;
		}
	}

	return res;
}

static gint
pop3_get_message_count (CamelFolder *folder)
{
	CamelPOP3Folder *pop3_folder = CAMEL_POP3_FOLDER (folder);

	return pop3_folder->uids->len;
}

static GPtrArray *
pop3_get_uids (CamelFolder *folder)
{
	CamelPOP3Folder *pop3_folder = CAMEL_POP3_FOLDER (folder);
	GPtrArray *uids = g_ptr_array_new();
	CamelPOP3FolderInfo **fi = (CamelPOP3FolderInfo **)pop3_folder->uids->pdata;
	gint i;

	for (i=0;i<pop3_folder->uids->len;i++,fi++) {
		if (fi[0]->uid)
			g_ptr_array_add(uids, fi[0]->uid);
	}

	return uids;
}
