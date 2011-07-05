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

#define d(x)

G_DEFINE_TYPE (CamelPOP3Folder, camel_pop3_folder, CAMEL_TYPE_FOLDER)

static void
cmd_uidl (CamelPOP3Engine *pe,
          CamelPOP3Stream *stream,
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
		ret = camel_pop3_stream_line (stream, &line, &len, NULL, NULL);
		if (ret>=0) {
			if (strlen ((gchar *) line) > 1024)
				line[1024] = 0;
			if (sscanf((gchar *) line, "%u %s", &id, uid) == 2) {
				fi = g_hash_table_lookup (folder->uids_id, GINT_TO_POINTER (id));
				if (fi) {
					camel_operation_progress (NULL, (fi->index+1) * 100 / folder->uids->len);
					fi->uid = g_strdup (uid);
					g_hash_table_insert (folder->uids_fi, fi->uid, fi);
				} else {
					g_warning("ID %u (uid: %s) not in previous LIST output", id, uid);
				}
			}
		}
	} while (ret>0);
}

/* create a uid from md5 of 'top' output */
static void
cmd_builduid (CamelPOP3Engine *pe,
              CamelPOP3Stream *stream,
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
	   We need a pointer to the folder perhaps? */
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
cmd_list (CamelPOP3Engine *pe, CamelPOP3Stream *stream, gpointer data)
{
	gint ret;
	guint len, id, size;
	guchar *line;
	CamelFolder *folder = data;
	CamelStore *parent_store;
	CamelPOP3Store *pop3_store;
	CamelPOP3FolderInfo *fi;

	parent_store = camel_folder_get_parent_store (folder);
	pop3_store = CAMEL_POP3_STORE (parent_store);

	do {
		ret = camel_pop3_stream_line (stream, &line, &len, NULL, NULL);
		if (ret>=0) {
			if (sscanf((gchar *) line, "%u %u", &id, &size) == 2) {
				fi = g_malloc0 (sizeof (*fi));
				fi->size = size;
				fi->id = id;
				fi->index = ((CamelPOP3Folder *) folder)->uids->len;
				if ((pop3_store->engine->capa & CAMEL_POP3_CAP_UIDL) == 0)
					fi->cmd = camel_pop3_engine_command_new(pe, CAMEL_POP3_COMMAND_MULTI, cmd_builduid, fi, NULL, NULL, "TOP %u 0\r\n", id);
				g_ptr_array_add (((CamelPOP3Folder *) folder)->uids, fi);
				g_hash_table_insert (((CamelPOP3Folder *) folder)->uids_id, GINT_TO_POINTER (id), fi);
			}
		}
	} while (ret>0);
}

static void
cmd_tocache (CamelPOP3Engine *pe,
             CamelPOP3Stream *stream,
             gpointer data)
{
	CamelPOP3FolderInfo *fi = data;
	gchar buffer[2048];
	gint w = 0, n;
	GError *error = NULL;

	/* What if it fails? */

	/* We write an '*' to the start of the stream to say its not complete yet */
	/* This should probably be part of the cache code */
	if ((n = camel_stream_write (fi->stream, "*", 1, NULL, &error)) == -1)
		goto done;

	while ((n = camel_stream_read ((CamelStream *) stream, buffer, sizeof (buffer), NULL, &error)) > 0) {
		n = camel_stream_write (fi->stream, buffer, n, NULL, &error);
		if (n == -1)
			break;

		w += n;
		if (w > fi->size)
			w = fi->size;
		if (fi->size != 0)
			camel_operation_progress (NULL, (w * 100) / fi->size);
	}

	/* it all worked, output a '#' to say we're a-ok */
	if (error == NULL) {
		camel_stream_reset (fi->stream, NULL);
		camel_stream_write (fi->stream, "#", 1, NULL, &error);
	}

done:
	if (error != NULL) {
		g_warning ("POP3 retrieval failed: %s", error->message);
		g_error_free (error);
	}

	g_object_unref (fi->stream);
	fi->stream = NULL;
}

static void
pop3_folder_dispose (GObject *object)
{
	CamelPOP3Folder *pop3_folder = CAMEL_POP3_FOLDER (object);
	CamelPOP3FolderInfo **fi = (CamelPOP3FolderInfo **) pop3_folder->uids->pdata;
	CamelPOP3Store *pop3_store = NULL;
	CamelStore *parent_store;

	parent_store = camel_folder_get_parent_store (CAMEL_FOLDER (object));
	if (parent_store)
		pop3_store = CAMEL_POP3_STORE (parent_store);

	if (pop3_folder->uids) {
		gint i;

		for (i = 0; i < pop3_folder->uids->len; i++, fi++) {
			if (fi[0]->cmd && pop3_store) {
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

	for (i=0;i<pop3_folder->uids->len;i++,fi++) {
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
		pop3_store->cache, "cache", fi->uid, NULL);
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

	g_return_val_if_fail (uid != NULL, NULL);

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

	/* Sigh, most of the crap in this function is so that the cancel button
	   returns the proper exception code.  Sigh. */

	camel_operation_push_message (
		cancellable, _("Retrieving POP message %d"), fi->id);

	/* If we have an oustanding retrieve message running, wait for that to complete
	   & then retrieve from cache, otherwise, start a new one, and similar */

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
	    || (stream = camel_data_cache_get(pop3_store->cache, "cache", fi->uid, NULL)) == NULL
	    || camel_stream_read (stream, buffer, 1, cancellable, NULL) != 1
	    || buffer[0] != '#') {

		/* Initiate retrieval, if disk backing fails, use a memory backing */
		if (pop3_store->cache == NULL
		    || (stream = camel_data_cache_add(pop3_store->cache, "cache", fi->uid, NULL)) == NULL)
			stream = camel_stream_mem_new ();

		/* ref it, the cache storage routine unref's when done */
		fi->stream = g_object_ref (stream);
		pcr = camel_pop3_engine_command_new(pop3_store->engine, CAMEL_POP3_COMMAND_MULTI, cmd_tocache, fi, NULL, NULL, "RETR %u\r\n", fi->id);

		/* Also initiate retrieval of some of the following messages, assume we'll be receiving them */
		if (pop3_store->cache != NULL) {
			/* This should keep track of the last one retrieved, also how many are still
			   oustanding incase of random access on large folders */
			i = fi->index+1;
			last = MIN (i+10, pop3_folder->uids->len);
			for (;i<last;i++) {
				CamelPOP3FolderInfo *pfi = pop3_folder->uids->pdata[i];

				if (pfi->uid && pfi->cmd == NULL) {
					pfi->stream = camel_data_cache_add(pop3_store->cache, "cache", pfi->uid, NULL);
					if (pfi->stream) {
						pfi->cmd = camel_pop3_engine_command_new (pop3_store->engine, CAMEL_POP3_COMMAND_MULTI,
											 cmd_tocache, pfi, NULL, NULL, "RETR %u\r\n", pfi->id);
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
		camel_stream_reset (stream, NULL);

		/* Check to see we have safely written flag set */
		if (i == -1) {
			g_prefix_error (
				error, _("Cannot get message %s: "), uid);
			goto done;
		}

		if (camel_stream_read (
			stream, buffer, 1, cancellable, error) == -1)
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

	camel_operation_push_message (
		cancellable, _("Retrieving POP summary"));

	/* only used during setup */
	pop3_folder->uids_id = g_hash_table_new (NULL, NULL);

	pcl = camel_pop3_engine_command_new(pop3_store->engine, CAMEL_POP3_COMMAND_MULTI, cmd_list, folder, cancellable, &local_error, "LIST\r\n");
	if (!local_error && (pop3_store->engine->capa & CAMEL_POP3_CAP_UIDL) != 0)
		pcu = camel_pop3_engine_command_new(pop3_store->engine, CAMEL_POP3_COMMAND_MULTI, cmd_uidl, folder, cancellable, &local_error, "UIDL\r\n");
	while ((i = camel_pop3_engine_iterate (pop3_store->engine, NULL, cancellable, error)) > 0)
		;

	if (local_error) {
		g_propagate_error (error, local_error);
		success = FALSE;
	} else if (i == -1) {
		g_prefix_error (error, _("Cannot get POP summary: "));
		success = FALSE;
	}

	/* TODO: check every id has a uid & commands returned OK too? */

	if (pcl)
		camel_pop3_engine_command_free (pop3_store->engine, pcl);

	if (pcu) {
		camel_pop3_engine_command_free (pop3_store->engine, pcu);
	} else {
		for (i=0;i<pop3_folder->uids->len;i++) {
			CamelPOP3FolderInfo *fi = pop3_folder->uids->pdata[i];
			if (fi->cmd) {
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

	if (!success)
		camel_service_disconnect_sync ((CamelService *) pop3_store, TRUE, NULL);

	return success;
}

static gboolean
pop3_folder_synchronize_sync (CamelFolder *folder,
                              gboolean expunge,
                              GCancellable *cancellable,
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

	if (pop3_store->delete_after > 0 && !expunge) {
		d(printf("%s(%d): pop3_store->delete_after = [%d], expunge=[%d]\n",
			 __FILE__, __LINE__, pop3_store->delete_after, expunge));
		camel_operation_push_message (
			cancellable, _("Expunging old messages"));

		camel_pop3_delete_old (
			folder, pop3_store->delete_after,
			cancellable, error);

		camel_operation_pop_message (cancellable);
	}

	if (!expunge || (pop3_store->keep_on_server && !pop3_store->delete_expunged)) {
		return TRUE;
	}

	camel_operation_push_message (
		cancellable, _("Expunging deleted messages"));

	for (i = 0; i < pop3_folder->uids->len; i++) {
		fi = pop3_folder->uids->pdata[i];
		/* busy already?  wait for that to finish first */
		if (fi->cmd) {
			while (camel_pop3_engine_iterate (pop3_store->engine, fi->cmd, NULL, NULL) > 0)
				;
			camel_pop3_engine_command_free (pop3_store->engine, fi->cmd);
			fi->cmd = NULL;
		}

		if (fi->flags & CAMEL_MESSAGE_DELETED) {
			fi->cmd = camel_pop3_engine_command_new (pop3_store->engine,
								0,
								NULL,
								NULL,
								NULL, NULL,
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
			while (camel_pop3_engine_iterate (pop3_store->engine, fi->cmd, NULL, NULL) > 0)
				;
			camel_pop3_engine_command_free (pop3_store->engine, fi->cmd);
			fi->cmd = NULL;
		}
		camel_operation_progress (
			cancellable, (i+1) * 100 / pop3_folder->uids->len);
	}

	camel_operation_pop_message (cancellable);

	camel_pop3_store_expunge (pop3_store, error);

	return TRUE;
}

static void
camel_pop3_folder_class_init (CamelPOP3FolderClass *class)
{
	GObjectClass *object_class;
	CamelFolderClass *folder_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->dispose = pop3_folder_dispose;

	folder_class = CAMEL_FOLDER_CLASS (class);
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

	d(printf("opening pop3 INBOX folder\n"));

	folder = g_object_new (
		CAMEL_TYPE_POP3_FOLDER,
		"full-name", "inbox", "display-name", "inbox",
		"parent-store", parent, NULL);

	/* mt-ok, since we dont have the folder-lock for new() */
	if (!camel_folder_refresh_info_sync (folder, cancellable, error)) {
		g_object_unref (folder);
		folder = NULL;
	}

	return folder;
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

gint
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

	pop3_folder = CAMEL_POP3_FOLDER (folder);
	pop3_store = CAMEL_POP3_STORE (parent_store);
	temp = time (&temp);

	d(printf("%s(%d): pop3_folder->uids->len=[%d]\n", __FILE__, __LINE__, pop3_folder->uids->len));
	for (i = 0; i < pop3_folder->uids->len; i++) {
		message_time = 0;
		fi = pop3_folder->uids->pdata[i];

		if (fi->cmd) {
			while (camel_pop3_engine_iterate (pop3_store->engine, fi->cmd, NULL, NULL) > 0) {
				; /* do nothing - iterating until end */
			}

			camel_pop3_engine_command_free (pop3_store->engine, fi->cmd);
			fi->cmd = NULL;
		}

		/* continue, if message wasn't received yet */
		if (!fi->uid)
			continue;

		d(printf("%s(%d): fi->uid=[%s]\n", __FILE__, __LINE__, fi->uid));
		if (!pop3_get_message_time_from_cache (folder, fi->uid, &message_time)) {
			d(printf("could not get message time from cache, trying from pop3\n"));
			message = pop3_folder_get_message_sync (
				folder, fi->uid, cancellable, error);
			if (message) {
				message_time = message->date + message->date_offset;
				g_object_unref (message);
			}
		}

		if (message_time) {
			gdouble time_diff = difftime (temp,message_time);
			gint day_lag = time_diff/(60*60*24);

			d(printf("%s(%d): message_time= [%ld]\n", __FILE__, __LINE__, message_time));
			d(printf("%s(%d): day_lag=[%d] \t days_to_delete=[%d]\n",
				__FILE__, __LINE__, day_lag, days_to_delete));

			if (day_lag > days_to_delete) {
				if (fi->cmd) {
					while (camel_pop3_engine_iterate (pop3_store->engine, fi->cmd, NULL, NULL) > 0) {
						; /* do nothing - iterating until end */
					}

					camel_pop3_engine_command_free (pop3_store->engine, fi->cmd);
					fi->cmd = NULL;
				}
				d(printf("%s(%d): Deleting old messages\n", __FILE__, __LINE__));
				fi->cmd = camel_pop3_engine_command_new (pop3_store->engine,
									0,
									NULL,
									NULL,
									NULL, NULL,
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
			while (camel_pop3_engine_iterate (pop3_store->engine, fi->cmd, NULL, NULL) > 0)
				;
			camel_pop3_engine_command_free (pop3_store->engine, fi->cmd);
			fi->cmd = NULL;
		}
		camel_operation_progress (
			cancellable, (i+1) * 100 / pop3_folder->uids->len);
	}

	camel_pop3_store_expunge (pop3_store, error);

	return 0;
}
