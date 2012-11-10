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

#include <stdlib.h>
#include <string.h>

#include <glib/gi18n-lib.h>

#include "camel-pop3-folder.h"
#include "camel-pop3-store.h"
#include "camel-pop3-settings.h"

#define d(x) if (camel_debug("pop3")) x;

G_DEFINE_TYPE (CamelPOP3Folder, camel_pop3_folder, CAMEL_TYPE_FOLDER)

static void
free_fi (CamelPOP3Folder *pop3_folder,CamelPOP3FolderInfo *fi)
{

	CamelPOP3Store *pop3_store;
	CamelStore *store;

	store = camel_folder_get_parent_store ((CamelFolder *) pop3_folder);
	pop3_store = CAMEL_POP3_STORE (store);

	g_hash_table_remove (pop3_folder->uids_id, GINT_TO_POINTER (fi->id));
	if (fi->cmd) {
		camel_pop3_engine_command_free (pop3_store->engine, fi->cmd);
		fi->cmd = NULL;
	}
	g_free (fi->uid);
	g_free (fi);

}
static void
cmd_uidl (CamelPOP3Engine *pe,
          CamelPOP3Stream *stream,
          GCancellable *cancellable,
          GError **error,
          gpointer data)
{
	gint ret;
	guint len;
	guchar *line;
	gchar uid[1025];
	guint id;
	CamelPOP3FolderInfo *fi;
	CamelPOP3Folder *folder = data;

	do {
		ret = camel_pop3_stream_line (stream, &line, &len, cancellable, error);
		if (ret >= 0) {
			if (strlen ((gchar *) line) > 1024)
				line[1024] = 0;
			if (sscanf ((gchar *) line, "%u %s", &id, uid) == 2) {
				fi = g_hash_table_lookup (folder->uids_id, GINT_TO_POINTER (id));
				if (fi) {
					camel_operation_progress (NULL, (fi->index + 1) * 100 / folder->uids->len);
					fi->uid = g_strdup (uid);
					g_hash_table_insert (folder->uids_fi, fi->uid, fi);
				} else {
					g_warning ("ID %u (uid: %s) not in previous LIST output", id, uid);
				}
			}
		}
	} while (ret > 0);
}

/* create a uid from md5 of 'top' output */
static void
cmd_builduid (CamelPOP3Engine *pe,
              CamelPOP3Stream *stream,
              GCancellable *cancellable,
              GError **error,
              gpointer data)
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
	 * We need a pointer to the folder perhaps? */
	camel_operation_progress (NULL, fi->id);

	checksum = g_checksum_new (G_CHECKSUM_MD5);
	mp = camel_mime_parser_new ();
	camel_mime_parser_init_with_stream (mp, (CamelStream *) stream, NULL);
	switch (camel_mime_parser_step (mp, NULL, NULL)) {
	case CAMEL_MIME_PARSER_STATE_HEADER:
	case CAMEL_MIME_PARSER_STATE_MESSAGE:
	case CAMEL_MIME_PARSER_STATE_MULTIPART:
		h = camel_mime_parser_headers_raw (mp);
		while (h) {
			if (g_ascii_strcasecmp (h->name, "status") != 0
			    && g_ascii_strcasecmp (h->name, "x-status") != 0) {
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

	d (printf ("building uid for id '%d' = '%s'\n", fi->id, fi->uid));
}

static void
cmd_list (CamelPOP3Engine *pe,
          CamelPOP3Stream *stream,
          GCancellable *cancellable,
          GError **error,
          gpointer data)
{
	gint ret;
	guint len, id, size;
	guchar *line;
	CamelFolder *folder = data;
	CamelStore *parent_store;
	CamelPOP3Store *pop3_store;
	CamelPOP3FolderInfo *fi;
	gint i = 0, total, last_uid=-1;
	CamelPOP3Folder *pop3_folder;
	CamelService *service;
	CamelSettings *settings;
	gint batch_fetch_count;

	parent_store = camel_folder_get_parent_store (folder);
	pop3_store = CAMEL_POP3_STORE (parent_store);
	pop3_folder = (CamelPOP3Folder *) folder;
	service = (CamelService *) parent_store;

	settings = camel_service_ref_settings (service);

	batch_fetch_count = camel_pop3_settings_get_batch_fetch_count (
		CAMEL_POP3_SETTINGS (settings));

	g_object_unref (settings);

	do {
		ret = camel_pop3_stream_line (stream, &line, &len, cancellable, error);
		if (ret >= 0) {
			if (sscanf ((gchar *) line, "%u %u", &id, &size) == 2) {
				fi = g_malloc0 (sizeof (*fi));
				fi->size = size;
				fi->id = id;
				fi->index = ((CamelPOP3Folder *) folder)->uids->len;
				if ((pop3_store->engine->capa & CAMEL_POP3_CAP_UIDL) == 0)
					fi->cmd = camel_pop3_engine_command_new (
						pe,
						CAMEL_POP3_COMMAND_MULTI,
						cmd_builduid, fi,
						cancellable, error,
						"TOP %u 0\r\n", id);
				g_ptr_array_add (pop3_folder->uids, fi);
				g_hash_table_insert (
					pop3_folder->uids_id,
					GINT_TO_POINTER (id), fi);
			}
		}
	} while (ret > 0);

	/* Trim the list for mobile devices*/
	if (pop3_folder->mobile_mode && pop3_folder->uids->len) {
		gint y = 0;
		gboolean save_uid = FALSE;

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
			d (printf ("Last stored' first uid: %d\n", last_uid));
		}

		if (last_uid == -1)
			save_uid = TRUE;

		for (i = total - 1; i >= 0; i--) {
			fi = pop3_folder->uids->pdata[i];

			if ((last_uid != -1 && last_uid >= fi->id) || (last_uid == -1 && i == total - batch_fetch_count)) {
				if (last_uid != -1 && last_uid < fi->id)
					i++; /* if the last uid was deleted on the server, then we need the last but 1 */
				break;
			}

		}
		if (i> 0 && pop3_folder->fetch_type == CAMEL_FETCH_OLD_MESSAGES && pop3_folder->fetch_more) {
			gint k = 0;
			/* Lets pull another window of old messages */
			save_uid = TRUE;
			/* Decrement 'i' by another batch count or till we reach the first message */
			d (printf ("Fetch more (%d): from %d", pop3_folder->fetch_more, i));
			for (k = 0; k< pop3_folder->fetch_more && i>= 0; k++, i--);
			d (printf (" to %d\n", i));

			/* Don't load messages newer than the latest we already had. We had to just get old messages and not 
			 * new messages. */
			for (y = i; y < total; y++) {
				fi = pop3_folder->uids->pdata[y];
				if (fi->id == pop3_folder->latest_id) {
					/* Delete everything after this. */

					for (y = k + 1; y < total; y++) {
						fi = pop3_folder->uids->pdata[y];
						free_fi (pop3_folder, fi);
					}
					g_ptr_array_remove_range (pop3_folder->uids, k + 1, total - k - 1);
					break;
				}
			}

		} else if (pop3_folder->fetch_more == CAMEL_FETCH_NEW_MESSAGES && pop3_folder->fetch_more) {
			/* We need to download new messages. */
			gint k = 0;

			for (k = i; k < total; k++) {
				fi = pop3_folder->uids->pdata[k];
				if (fi->id == pop3_folder->latest_id) {
					/* We need to just download the specified number of messages. */
					y= (k + pop3_folder->fetch_more) < total ? (k + pop3_folder->fetch_more) : total - 1;
					break;
				}
			}

		}

		/* Delete the unnecessary old messages */
		if (i > 0) {
			gint j = 0;
			/* i is the start of the last fetch UID, so remove everything else from 0 to i */
			for (; j < i; j++) {
				fi = pop3_folder->uids->pdata[j];
				free_fi (pop3_folder, fi);
			}
			g_ptr_array_remove_range (pop3_folder->uids, 0, i);
			d (printf ("Removing %d uids that are old\n", i));

		}

		/* Delete the unnecessary new message references. */
		if (y + 1 < total) {
			gint k;

			for (k = y + 1; k < total; k++) {
				fi = pop3_folder->uids->pdata[k];
				free_fi (pop3_folder, fi);
			}
			g_ptr_array_remove_range (pop3_folder->uids, y + 1, total - y - 1);
		}

		if (save_uid) {
			gchar *contents;
			gsize len;
			const gchar *root;
			gchar *path;

			/* Save the last fetched UID */
			fi = pop3_folder->uids->pdata[0];
			g_key_file_set_integer (pop3_folder->key_file, "UIDConfig", "last-saved-uid", fi->id);
			contents = g_key_file_to_data (pop3_folder->key_file, &len, NULL);
			root = camel_service_get_user_cache_dir (service);
			path = g_build_filename (root, "uidconfig", NULL);
			g_file_set_contents (path, contents, len, NULL);
			g_key_file_load_from_file (pop3_folder->key_file, path, G_KEY_FILE_NONE, NULL);
			g_free (contents);
			g_free (path);
			d (printf ("Saving last uid %d\n", fi->id));

		}

	}

}

static void
cmd_tocache (CamelPOP3Engine *pe,
             CamelPOP3Stream *stream,
             GCancellable *cancellable,
             GError **error,
             gpointer data)
{
	CamelPOP3FolderInfo *fi = data;
	gchar buffer[2048];
	gint w = 0, n;
	GError *local_error = NULL;

	/* What if it fails? */

	/* We write an '*' to the start of the stream to say its not complete yet */
	/* This should probably be part of the cache code */
	if ((n = camel_stream_write (fi->stream, "*", 1, cancellable, &local_error)) == -1)
		goto done;

	while ((n = camel_stream_read ((CamelStream *) stream, buffer, sizeof (buffer), cancellable, &local_error)) > 0) {
		n = camel_stream_write (fi->stream, buffer, n, cancellable, &local_error);
		if (n == -1)
			break;

		w += n;
		if (w > fi->size)
			w = fi->size;
		if (fi->size != 0)
			camel_operation_progress (NULL, (w * 100) / fi->size);
	}

	/* it all worked, output a '#' to say we're a-ok */
	if (local_error == NULL) {
		g_seekable_seek (
			G_SEEKABLE (fi->stream),
			0, G_SEEK_SET, cancellable, NULL);
		camel_stream_write (fi->stream, "#", 1, cancellable, &local_error);
	}

done:
	if (local_error != NULL) {
		g_propagate_error (error, local_error);
	}

	g_object_unref (fi->stream);
	fi->stream = NULL;
}

static void
pop3_folder_dispose (GObject *object)
{
	CamelPOP3Folder *pop3_folder = CAMEL_POP3_FOLDER (object);
	CamelPOP3Store *pop3_store = NULL;
	CamelStore *parent_store;

	parent_store = camel_folder_get_parent_store (CAMEL_FOLDER (object));
	if (parent_store)
		pop3_store = CAMEL_POP3_STORE (parent_store);

	if (pop3_folder->uids) {
		gint i;
		CamelPOP3FolderInfo **fi = (CamelPOP3FolderInfo **) pop3_folder->uids->pdata;
		gboolean is_online = camel_service_get_connection_status (CAMEL_SERVICE (parent_store)) == CAMEL_SERVICE_CONNECTED;

		for (i = 0; i < pop3_folder->uids->len; i++, fi++) {
			if (fi[0]->cmd && pop3_store && is_online) {
				while (camel_pop3_engine_iterate (pop3_store->engine, fi[0]->cmd, NULL, NULL) > 0)
					;
				camel_pop3_engine_command_free (pop3_store->engine, fi[0]->cmd);
			}

			g_free (fi[0]->uid);
			g_free (fi[0]);
		}

		g_ptr_array_free (pop3_folder->uids, TRUE);
		pop3_folder->uids = NULL;
	}

	if (pop3_folder->uids_fi) {
		g_hash_table_destroy (pop3_folder->uids_fi);
		pop3_folder->uids_fi = NULL;
	}

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (camel_pop3_folder_parent_class)->dispose (object);
}

static gint
pop3_folder_get_message_count (CamelFolder *folder)
{
	CamelPOP3Folder *pop3_folder = CAMEL_POP3_FOLDER (folder);

	return pop3_folder->uids->len;
}

static GPtrArray *
pop3_folder_get_uids (CamelFolder *folder)
{
	CamelPOP3Folder *pop3_folder = CAMEL_POP3_FOLDER (folder);
	GPtrArray *uids = g_ptr_array_new ();
	CamelPOP3FolderInfo **fi = (CamelPOP3FolderInfo **) pop3_folder->uids->pdata;
	gint i;

	for (i = 0; i < pop3_folder->uids->len; i++,fi++) {
		if (fi[0]->uid)
			g_ptr_array_add (uids, fi[0]->uid);
	}

	return uids;
}

static gchar *
pop3_folder_get_filename (CamelFolder *folder,
                          const gchar *uid,
                          GError **error)
{
	CamelStore *parent_store;
	CamelPOP3Folder *pop3_folder;
	CamelPOP3Store *pop3_store;
	CamelPOP3FolderInfo *fi;

	parent_store = camel_folder_get_parent_store (folder);

	pop3_folder = CAMEL_POP3_FOLDER (folder);
	pop3_store = CAMEL_POP3_STORE (parent_store);

	fi = g_hash_table_lookup (pop3_folder->uids_fi, uid);
	if (fi == NULL) {
		g_set_error (
			error, CAMEL_FOLDER_ERROR,
			CAMEL_FOLDER_ERROR_INVALID_UID,
			_("No message with UID %s"), uid);
		return NULL;
	}

	return camel_data_cache_get_filename (
		pop3_store->cache, "cache", fi->uid);
}

static gboolean
pop3_folder_set_message_flags (CamelFolder *folder,
                               const gchar *uid,
                               CamelMessageFlags flags,
                               CamelMessageFlags set)
{
	CamelPOP3Folder *pop3_folder = CAMEL_POP3_FOLDER (folder);
	CamelPOP3FolderInfo *fi;
	gboolean res = FALSE;

	fi = g_hash_table_lookup (pop3_folder->uids_fi, uid);
	if (fi) {
		guint32 new = (fi->flags & ~flags) | (set & flags);

		if (fi->flags != new) {
			fi->flags = new;
			res = TRUE;
		}
	}

	return res;
}

static CamelMimeMessage *
pop3_folder_get_message_sync (CamelFolder *folder,
                              const gchar *uid,
                              GCancellable *cancellable,
                              GError **error)
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
	CamelService *service;
	CamelSettings *settings;
	gboolean auto_fetch;

	g_return_val_if_fail (uid != NULL, NULL);

	parent_store = camel_folder_get_parent_store (folder);

	pop3_folder = CAMEL_POP3_FOLDER (folder);
	pop3_store = CAMEL_POP3_STORE (parent_store);

	service = CAMEL_SERVICE (parent_store);

	settings = camel_service_ref_settings (service);

	g_object_get (
		settings,
		"auto-fetch", &auto_fetch,
		NULL);

	g_object_unref (settings);

	fi = g_hash_table_lookup (pop3_folder->uids_fi, uid);
	if (fi == NULL) {
		g_set_error (
			error, CAMEL_FOLDER_ERROR,
			CAMEL_FOLDER_ERROR_INVALID_UID,
			_("No message with UID %s"), uid);
		return NULL;
	}

	if (camel_service_get_connection_status (CAMEL_SERVICE (parent_store)) != CAMEL_SERVICE_CONNECTED) {
		g_set_error (
			error, CAMEL_SERVICE_ERROR,
			CAMEL_SERVICE_ERROR_UNAVAILABLE,
			_("You must be working online to complete this operation"));
		return FALSE;
	}

	/* Sigh, most of the crap in this function is so that the cancel button
	 * returns the proper exception code.  Sigh. */

	camel_operation_push_message (
		cancellable, _("Retrieving POP message %d"), fi->id);

	/* If we have an oustanding retrieve message running, wait for that to complete
	 * & then retrieve from cache, otherwise, start a new one, and similar */

	if (fi->cmd != NULL) {
		while ((i = camel_pop3_engine_iterate (pop3_store->engine, fi->cmd, cancellable, error)) > 0)
			;

		/* getting error code? */
		/*g_assert (fi->cmd->state == CAMEL_POP3_COMMAND_DATA);*/
		camel_pop3_engine_command_free (pop3_store->engine, fi->cmd);
		fi->cmd = NULL;

		if (i == -1) {
			g_prefix_error (
				error, _("Cannot get message %s: "), uid);
			goto fail;
		}
	}

	/* check to see if we have safely written flag set */
	if (pop3_store->cache == NULL
	    || (stream = camel_data_cache_get (pop3_store->cache, "cache", fi->uid, NULL)) == NULL
	    || camel_stream_read (stream, buffer, 1, cancellable, NULL) != 1
	    || buffer[0] != '#') {

		/* Initiate retrieval, if disk backing fails, use a memory backing */
		if (pop3_store->cache == NULL
		    || (stream = camel_data_cache_add (pop3_store->cache, "cache", fi->uid, NULL)) == NULL)
			stream = camel_stream_mem_new ();

		/* ref it, the cache storage routine unref's when done */
		fi->stream = g_object_ref (stream);
		pcr = camel_pop3_engine_command_new (
			pop3_store->engine,
			CAMEL_POP3_COMMAND_MULTI,
			cmd_tocache, fi,
			cancellable, error,
			"RETR %u\r\n", fi->id);

		/* Also initiate retrieval of some of the following
		 * messages, assume we'll be receiving them. */
		if (auto_fetch && pop3_store->cache != NULL) {
			/* This should keep track of the last one retrieved,
			 * also how many are still oustanding incase of random
			 * access on large folders. */
			i = fi->index + 1;
			last = MIN (i + 10, pop3_folder->uids->len);
			for (; i < last; i++) {
				CamelPOP3FolderInfo *pfi = pop3_folder->uids->pdata[i];

				if (pfi->uid && pfi->cmd == NULL) {
					pfi->stream = camel_data_cache_add (
						pop3_store->cache,
						"cache", pfi->uid, NULL);
					if (pfi->stream) {
						pfi->cmd = camel_pop3_engine_command_new (
							pop3_store->engine,
							CAMEL_POP3_COMMAND_MULTI,
							cmd_tocache, pfi,
							cancellable, error,
							"RETR %u\r\n", pfi->id);
					}
				}
			}
		}

		/* now wait for the first one to finish */
		while ((i = camel_pop3_engine_iterate (pop3_store->engine, pcr, cancellable, error)) > 0)
			;

		/* getting error code? */
		/*g_assert (pcr->state == CAMEL_POP3_COMMAND_DATA);*/
		camel_pop3_engine_command_free (pop3_store->engine, pcr);
		g_seekable_seek (
			G_SEEKABLE (stream), 0, G_SEEK_SET, NULL, NULL);

		/* Check to see we have safely written flag set */
		if (i == -1) {
			g_prefix_error (
				error, _("Cannot get message %s: "), uid);
			goto done;
		}

		if (camel_stream_read (stream, buffer, 1, cancellable, error) == -1)
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
	if (!camel_data_wrapper_construct_from_stream_sync (
		CAMEL_DATA_WRAPPER (message), stream, cancellable, error)) {
		g_prefix_error (error, _("Cannot get message %s: "), uid);
		g_object_unref (message);
		message = NULL;
	} else {
		/* because the UID in the local store doesn't match with the UID in the pop3 store */
		camel_medium_add_header (CAMEL_MEDIUM (message), "X-Evolution-POP3-UID", uid);
	}
done:
	g_object_unref (stream);
fail:
	camel_operation_pop_message (cancellable);

	return message;
}

static gboolean
pop3_folder_refresh_info_sync (CamelFolder *folder,
                               GCancellable *cancellable,
                               GError **error)
{
	CamelStore *parent_store;
	CamelPOP3Store *pop3_store;
	CamelPOP3Folder *pop3_folder = (CamelPOP3Folder *) folder;
	CamelPOP3Command *pcl, *pcu = NULL;
	gboolean success = TRUE;
	GError *local_error = NULL;
	gint i;

	parent_store = camel_folder_get_parent_store (folder);
	pop3_store = CAMEL_POP3_STORE (parent_store);

	if (camel_service_get_connection_status (CAMEL_SERVICE (parent_store)) != CAMEL_SERVICE_CONNECTED) {
		g_set_error (
			error, CAMEL_SERVICE_ERROR,
			CAMEL_SERVICE_ERROR_UNAVAILABLE,
			_("You must be working online to complete this operation"));
		return FALSE;
	}

	camel_operation_push_message (
		cancellable, _("Retrieving POP summary"));

	/* Get rid of the old cache */
	if (pop3_folder->uids) {
		gint i;
		CamelPOP3FolderInfo *last_fi;

		if (pop3_folder->uids->len) {
			last_fi = pop3_folder->uids->pdata[pop3_folder->uids->len - 1];
			if (last_fi)
				pop3_folder->latest_id = last_fi->id;
			else
				pop3_folder->latest_id = -1;
		} else
			pop3_folder->latest_id = -1;

		for (i = 0; i < pop3_folder->uids->len; i++) {
			CamelPOP3FolderInfo *fi = pop3_folder->uids->pdata[i];
			if (fi->cmd) {
				camel_pop3_engine_command_free (pop3_store->engine, fi->cmd);
				fi->cmd = NULL;
			}
			g_free (fi->uid);
			g_free (fi);
		}

		g_ptr_array_free (pop3_folder->uids, TRUE);
	}

	if (pop3_folder->uids_fi) {
		g_hash_table_destroy (pop3_folder->uids_fi);
		pop3_folder->uids_fi = NULL;
	}

	/* Get a new working set. */
	pop3_folder->uids = g_ptr_array_new ();
	pop3_folder->uids_fi = g_hash_table_new (g_str_hash, g_str_equal);

	/* only used during setup */
	pop3_folder->uids_id = g_hash_table_new (NULL, NULL);

	pcl = camel_pop3_engine_command_new (
		pop3_store->engine,
		CAMEL_POP3_COMMAND_MULTI,
		cmd_list, folder,
		cancellable, &local_error,
		"LIST\r\n");
	if (!local_error && (pop3_store->engine->capa & CAMEL_POP3_CAP_UIDL) != 0)
		pcu = camel_pop3_engine_command_new (
			pop3_store->engine,
			CAMEL_POP3_COMMAND_MULTI,
			cmd_uidl, folder,
			cancellable, &local_error,
			"UIDL\r\n");
	while ((i = camel_pop3_engine_iterate (pop3_store->engine, NULL, cancellable, &local_error)) > 0)
		;

	if (local_error) {
		g_propagate_error (error, local_error);
		g_prefix_error (error, _("Cannot get POP summary: "));
		success = FALSE;
	} else if (i == -1) {
		g_set_error_literal (error, CAMEL_ERROR, CAMEL_ERROR_GENERIC, _("Cannot get POP summary: "));
		success = FALSE;
	}

	/* TODO: check every id has a uid & commands returned OK too? */

	if (pcl) {
		if (success && pcl->state == CAMEL_POP3_COMMAND_ERR) {
			success = FALSE;

			if (pcl->error_str)
				g_set_error_literal (error, CAMEL_ERROR, CAMEL_ERROR_GENERIC, pcl->error_str);
			else
				g_set_error_literal (error, CAMEL_ERROR, CAMEL_ERROR_GENERIC, _("Cannot get POP summary: "));
		}

		camel_pop3_engine_command_free (pop3_store->engine, pcl);
	}

	if (pcu) {
		if (success && pcu->state == CAMEL_POP3_COMMAND_ERR) {
			success = FALSE;

			if (pcu->error_str)
				g_set_error_literal (error, CAMEL_ERROR, CAMEL_ERROR_GENERIC, pcu->error_str);
			else
				g_set_error_literal (error, CAMEL_ERROR, CAMEL_ERROR_GENERIC, _("Cannot get POP summary: "));
		}

		camel_pop3_engine_command_free (pop3_store->engine, pcu);
	} else {
		for (i = 0; i < pop3_folder->uids->len; i++) {
			CamelPOP3FolderInfo *fi = pop3_folder->uids->pdata[i];
			if (fi->cmd) {
				if (success && fi->cmd->state == CAMEL_POP3_COMMAND_ERR) {
					success = FALSE;

					if (fi->cmd->error_str)
						g_set_error_literal (error, CAMEL_ERROR, CAMEL_ERROR_GENERIC, fi->cmd->error_str);
					else
						g_set_error_literal (error, CAMEL_ERROR, CAMEL_ERROR_GENERIC, _("Cannot get POP summary: "));
				}

				camel_pop3_engine_command_free (pop3_store->engine, fi->cmd);
				fi->cmd = NULL;
			}
			if (fi->uid) {
				g_hash_table_insert (pop3_folder->uids_fi, fi->uid, fi);
			}
		}
	}

	/* dont need this anymore */
	g_hash_table_destroy (pop3_folder->uids_id);
	pop3_folder->uids_id = NULL;

	camel_operation_pop_message (cancellable);

	return success;
}

static gboolean
pop3_fetch_messages_sync (CamelFolder *folder,
                          CamelFetchType type,
                          gint limit,
                          GCancellable *cancellable,
                          GError **error)
{
	CamelPOP3FolderInfo *fi;
	CamelPOP3Folder *pop3_folder = (CamelPOP3Folder *) folder;
	gint old_len;
	CamelStore *parent_store;
	CamelService *service;
	CamelSettings *settings;
	gint batch_fetch_count;

	parent_store = camel_folder_get_parent_store (folder);
	service = (CamelService *) parent_store;

	settings = camel_service_ref_settings (service);

	batch_fetch_count = camel_pop3_settings_get_batch_fetch_count (
		CAMEL_POP3_SETTINGS (settings));

	g_object_unref (settings);

	old_len = pop3_folder->uids->len;

	/* If we have the first message already, then return FALSE */
	fi = pop3_folder->uids->pdata[0];
	if (type == CAMEL_FETCH_OLD_MESSAGES && fi->id == pop3_folder->first_id)
		return FALSE;

	pop3_folder->fetch_type = type;
	pop3_folder->fetch_more = (limit > 0) ? limit : batch_fetch_count;
	pop3_folder_refresh_info_sync (folder, cancellable, error);
	pop3_folder->fetch_more = 0;

	/* Even if we downloaded the first/oldest message, just now, return TRUE so that we wont waste another cycle */
	fi = pop3_folder->uids->pdata[0];
	if (type == CAMEL_FETCH_OLD_MESSAGES && fi->id == pop3_folder->first_id)
		return FALSE;
	else if (type == CAMEL_FETCH_NEW_MESSAGES && old_len == pop3_folder->uids->len)
		return FALSE; /* We didnt fetch any new messages as there were none probably. */

	return TRUE;
}

static gboolean
pop3_folder_synchronize_sync (CamelFolder *folder,
                              gboolean expunge,
                              GCancellable *cancellable,
                              GError **error)
{
	CamelService *service;
	CamelSettings *settings;
	CamelStore *parent_store;
	CamelPOP3Folder *pop3_folder;
	CamelPOP3Store *pop3_store;
	CamelPOP3FolderInfo *fi;
	gint delete_after_days;
	gboolean delete_expunged;
	gboolean keep_on_server;
	gboolean is_online;
	gint i;

	parent_store = camel_folder_get_parent_store (folder);

	pop3_folder = CAMEL_POP3_FOLDER (folder);
	pop3_store = CAMEL_POP3_STORE (parent_store);

	service = CAMEL_SERVICE (parent_store);
	is_online = camel_service_get_connection_status (service) == CAMEL_SERVICE_CONNECTED;

	settings = camel_service_ref_settings (service);

	g_object_get (
		settings,
		"delete-after-days", &delete_after_days,
		"delete-expunged", &delete_expunged,
		"keep-on-server", &keep_on_server,
		NULL);

	g_object_unref (settings);

	if (is_online && delete_after_days > 0 && !expunge) {
		camel_operation_push_message (
			cancellable, _("Expunging old messages"));

		camel_pop3_delete_old (
			folder, delete_after_days, cancellable, error);

		camel_operation_pop_message (cancellable);
	}

	if (!expunge || (keep_on_server && !delete_expunged))
		return TRUE;

	if (!is_online) {
		g_set_error (
			error, CAMEL_SERVICE_ERROR,
			CAMEL_SERVICE_ERROR_UNAVAILABLE,
			_("You must be working online to complete this operation"));
		return FALSE;
	}

	camel_operation_push_message (
		cancellable, _("Expunging deleted messages"));

	for (i = 0; i < pop3_folder->uids->len; i++) {
		fi = pop3_folder->uids->pdata[i];
		/* busy already?  wait for that to finish first */
		if (fi->cmd) {
			while (camel_pop3_engine_iterate (pop3_store->engine, fi->cmd, cancellable, NULL) > 0)
				;
			camel_pop3_engine_command_free (pop3_store->engine, fi->cmd);
			fi->cmd = NULL;
		}

		if (fi->flags & CAMEL_MESSAGE_DELETED) {
			fi->cmd = camel_pop3_engine_command_new (
				pop3_store->engine,
				0, NULL, NULL,
				cancellable, NULL,
				"DELE %u\r\n", fi->id);

			/* also remove from cache */
			if (pop3_store->cache && fi->uid)
				camel_data_cache_remove (pop3_store->cache, "cache", fi->uid, NULL);
		}
	}

	for (i = 0; i < pop3_folder->uids->len; i++) {
		fi = pop3_folder->uids->pdata[i];
		/* wait for delete commands to finish */
		if (fi->cmd) {
			while (camel_pop3_engine_iterate (pop3_store->engine, fi->cmd, cancellable, NULL) > 0)
				;
			camel_pop3_engine_command_free (pop3_store->engine, fi->cmd);
			fi->cmd = NULL;
		}
		camel_operation_progress (
			cancellable, (i + 1) * 100 / pop3_folder->uids->len);
	}

	camel_operation_pop_message (cancellable);

	return camel_pop3_store_expunge (pop3_store, cancellable, error);
}

static void
camel_pop3_folder_class_init (CamelPOP3FolderClass *class)
{
	GObjectClass *object_class;
	CamelFolderClass *folder_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->dispose = pop3_folder_dispose;

	folder_class = CAMEL_FOLDER_CLASS (class);
	folder_class->fetch_messages_sync = pop3_fetch_messages_sync;
	folder_class->get_message_count = pop3_folder_get_message_count;
	folder_class->get_uids = pop3_folder_get_uids;
	folder_class->free_uids = camel_folder_free_shallow;
	folder_class->get_filename = pop3_folder_get_filename;
	folder_class->set_message_flags = pop3_folder_set_message_flags;
	folder_class->get_message_sync = pop3_folder_get_message_sync;
	folder_class->refresh_info_sync = pop3_folder_refresh_info_sync;
	folder_class->synchronize_sync = pop3_folder_synchronize_sync;
}

static void
camel_pop3_folder_init (CamelPOP3Folder *pop3_folder)
{
	pop3_folder->uids = g_ptr_array_new ();
	pop3_folder->uids_fi = g_hash_table_new (g_str_hash, g_str_equal);
}

CamelFolder *
camel_pop3_folder_new (CamelStore *parent,
                       GCancellable *cancellable,
                       GError **error)
{
	CamelFolder *folder;
	CamelService *service;
	CamelSettings *settings;
	CamelPOP3Folder *pop3_folder;

	service = CAMEL_SERVICE (parent);

	d (printf ("opening pop3 INBOX folder\n"));

	folder = g_object_new (
		CAMEL_TYPE_POP3_FOLDER,
		"full-name", "inbox", "display-name", "inbox",
		"parent-store", parent, NULL);

	settings = camel_service_ref_settings (service);

	pop3_folder = (CamelPOP3Folder *) folder;
	pop3_folder->mobile_mode = camel_pop3_settings_get_mobile_mode (
		CAMEL_POP3_SETTINGS (settings));

	g_object_unref (settings);

	pop3_folder->fetch_more = 0;
	if (pop3_folder->mobile_mode) {
		/* Setup Keyfile */
		gchar *path;
		const gchar *root;

		pop3_folder->key_file = g_key_file_new ();
		root = camel_service_get_user_cache_dir (service);
		path = g_build_filename (root, "uidconfig", NULL);
		g_key_file_load_from_file (pop3_folder->key_file, path, G_KEY_FILE_NONE, NULL);

		g_free (path);
	}

	if (camel_service_get_connection_status (CAMEL_SERVICE (parent)) != CAMEL_SERVICE_CONNECTED)
		return folder;

	/* mt-ok, since we dont have the folder-lock for new() */
	if (!camel_folder_refresh_info_sync (folder, cancellable, error)) {
		g_object_unref (folder);
		folder = NULL;
	}

	return folder;
}

static gboolean
pop3_get_message_time_from_cache (CamelFolder *folder,
                                  const gchar *uid,
                                  time_t *message_time)
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
	    && camel_stream_read (stream, buffer, 1, NULL, NULL) == 1
	    && buffer[0] == '#') {
		CamelMimeMessage *message;
		GError *error = NULL;

		message = camel_mime_message_new ();
		camel_data_wrapper_construct_from_stream_sync (
			(CamelDataWrapper *) message, stream, NULL, &error);
		if (error != NULL) {
			g_warning (_("Cannot get message %s: %s"), uid, error->message);
			g_error_free (error);

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

gboolean
camel_pop3_delete_old (CamelFolder *folder,
                       gint days_to_delete,
                       GCancellable *cancellable,
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

	if (camel_service_get_connection_status (CAMEL_SERVICE (parent_store)) != CAMEL_SERVICE_CONNECTED) {
		g_set_error (
			error, CAMEL_SERVICE_ERROR,
			CAMEL_SERVICE_ERROR_UNAVAILABLE,
			_("You must be working online to complete this operation"));
		return FALSE;
	}

	pop3_folder = CAMEL_POP3_FOLDER (folder);
	pop3_store = CAMEL_POP3_STORE (parent_store);
	temp = time (&temp);

	d (printf ("%s(%d): pop3_folder->uids->len=[%d]\n", __FILE__, __LINE__, pop3_folder->uids->len));
	for (i = 0; i < pop3_folder->uids->len; i++) {
		message_time = 0;
		fi = pop3_folder->uids->pdata[i];

		if (fi->cmd) {
			while (camel_pop3_engine_iterate (pop3_store->engine, fi->cmd, cancellable, NULL) > 0) {
				; /* do nothing - iterating until end */
			}

			camel_pop3_engine_command_free (pop3_store->engine, fi->cmd);
			fi->cmd = NULL;
		}

		/* continue, if message wasn't received yet */
		if (!fi->uid)
			continue;

		d (printf ("%s(%d): fi->uid=[%s]\n", __FILE__, __LINE__, fi->uid));
		if (!pop3_get_message_time_from_cache (folder, fi->uid, &message_time)) {
			d (printf ("could not get message time from cache, trying from pop3\n"));
			message = pop3_folder_get_message_sync (
				folder, fi->uid, cancellable, error);
			if (message) {
				message_time = message->date + message->date_offset;
				g_object_unref (message);
			}
		}

		if (message_time) {
			gdouble time_diff = difftime (temp,message_time);
			gint day_lag = time_diff / (60 * 60 * 24);

			d (printf (
				"%s(%d): message_time= [%ld]\n",
				__FILE__, __LINE__, message_time));
			d (printf (
				"%s(%d): day_lag=[%d] \t days_to_delete=[%d]\n",
				__FILE__, __LINE__, day_lag, days_to_delete));

			if (day_lag > days_to_delete) {
				if (fi->cmd) {
					while (camel_pop3_engine_iterate (pop3_store->engine, fi->cmd, cancellable, NULL) > 0) {
						; /* do nothing - iterating until end */
					}

					camel_pop3_engine_command_free (pop3_store->engine, fi->cmd);
					fi->cmd = NULL;
				}
				d (printf (
					"%s(%d): Deleting old messages\n",
					__FILE__, __LINE__));
				fi->cmd = camel_pop3_engine_command_new (
					pop3_store->engine,
					0, NULL, NULL,
					cancellable, NULL,
					"DELE %u\r\n", fi->id);
				/* also remove from cache */
				if (pop3_store->cache && fi->uid) {
					camel_data_cache_remove (pop3_store->cache, "cache", fi->uid, NULL);
				}
			}
		}
	}

	for (i = 0; i < pop3_folder->uids->len; i++) {
		fi = pop3_folder->uids->pdata[i];
		/* wait for delete commands to finish */
		if (fi->cmd) {
			while (camel_pop3_engine_iterate (pop3_store->engine, fi->cmd, cancellable, NULL) > 0)
				;
			camel_pop3_engine_command_free (pop3_store->engine, fi->cmd);
			fi->cmd = NULL;
		}
		camel_operation_progress (
			cancellable, (i + 1) * 100 / pop3_folder->uids->len);
	}

	return camel_pop3_store_expunge (pop3_store, cancellable, error);
}
