/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-disco-diary.c: class for a disconnected operation log */

/*
 * Authors: Dan Winship <danw@ximian.com>
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

#define __USE_LARGEFILE 1
#include <stdio.h>
#include <errno.h>

#include <glib/gstdio.h>
#include <glib/gi18n-lib.h>

#include "camel-disco-diary.h"
#include "camel-disco-folder.h"
#include "camel-disco-store.h"
#include "camel-file-utils.h"
#include "camel-folder.h"
#include "camel-operation.h"
#include "camel-session.h"
#include "camel-store.h"

#define d(x)

G_DEFINE_TYPE (CamelDiscoDiary, camel_disco_diary, CAMEL_TYPE_OBJECT)

static void
unref_folder (gpointer key,
              gpointer value,
              gpointer data)
{
	g_object_unref (value);
}

static void
free_uid (gpointer key,
          gpointer value,
          gpointer data)
{
	g_free (key);
	g_free (value);
}

static void
disco_diary_finalize (GObject *object)
{
	CamelDiscoDiary *diary = CAMEL_DISCO_DIARY (object);

	if (diary->file)
		fclose (diary->file);

	if (diary->folders) {
		g_hash_table_foreach (diary->folders, unref_folder, NULL);
		g_hash_table_destroy (diary->folders);
	}

	if (diary->uidmap) {
		g_hash_table_foreach (diary->uidmap, free_uid, NULL);
		g_hash_table_destroy (diary->uidmap);
	}

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (camel_disco_diary_parent_class)->finalize (object);
}

static void
camel_disco_diary_class_init (CamelDiscoDiaryClass *class)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->finalize = disco_diary_finalize;
}

static void
camel_disco_diary_init (CamelDiscoDiary *diary)
{
	diary->folders = g_hash_table_new (g_str_hash, g_str_equal);
	diary->uidmap = g_hash_table_new (g_str_hash, g_str_equal);
}

static gint
diary_encode_uids (CamelDiscoDiary *diary,
                   GPtrArray *uids)
{
	gint i, status;

	status = camel_file_util_encode_uint32 (diary->file, uids->len);
	for (i = 0; status != -1 && i < uids->len; i++)
		status = camel_file_util_encode_string (diary->file, uids->pdata[i]);
	return status;
}

void
camel_disco_diary_log (CamelDiscoDiary *diary,
                       CamelDiscoDiaryAction action,
                       ...)
{
	va_list ap;
	gint status;

	d (printf ("diary log: %s\n", diary->file?"ok":"no file!"));

	/* You may already be a loser. */
	if (!diary || !diary->file)
		return;

	status = camel_file_util_encode_uint32 (diary->file, action);
	if (status == -1)
		goto lose;

	va_start (ap, action);
	switch (action) {
	case CAMEL_DISCO_DIARY_FOLDER_EXPUNGE:
	{
		CamelFolder *folder = va_arg (ap, CamelFolder *);
		GPtrArray *uids = va_arg (ap, GPtrArray *);
		const gchar *full_name;

		d (printf (" folder expunge '%s'\n", folder->full_name));

		full_name = camel_folder_get_full_name (folder);
		status = camel_file_util_encode_string (diary->file, full_name);
		if (status != -1)
			status = diary_encode_uids (diary, uids);
		break;
	}

	case CAMEL_DISCO_DIARY_FOLDER_APPEND:
	{
		CamelFolder *folder = va_arg (ap, CamelFolder *);
		gchar *uid = va_arg (ap, gchar *);
		const gchar *full_name;

		d (printf (" folder append '%s'\n", folder->full_name));

		full_name = camel_folder_get_full_name (folder);
		status = camel_file_util_encode_string (diary->file, full_name);
		if (status != -1)
			status = camel_file_util_encode_string (diary->file, uid);
		break;
	}

	case CAMEL_DISCO_DIARY_FOLDER_TRANSFER:
	{
		CamelFolder *source = va_arg (ap, CamelFolder *);
		CamelFolder *destination = va_arg (ap, CamelFolder *);
		GPtrArray *uids = va_arg (ap, GPtrArray *);
		gboolean delete_originals = va_arg (ap, gboolean);
		const gchar *full_name;

		full_name = camel_folder_get_full_name (source);
		status = camel_file_util_encode_string (diary->file, full_name);
		if (status == -1)
			break;

		full_name = camel_folder_get_full_name (destination);
		status = camel_file_util_encode_string (diary->file, full_name);
		if (status == -1)
			break;

		status = diary_encode_uids (diary, uids);
		if (status == -1)
			break;
		status = camel_file_util_encode_uint32 (diary->file, delete_originals);
		break;
	}

	default:
		g_assert_not_reached ();
		break;
	}

	va_end (ap);

 lose:
	if (status == -1) {
		CamelService *service;
		CamelSession *session;
		gchar *msg;

		service = CAMEL_SERVICE (diary->store);
		session = camel_service_get_session (service);

		msg = g_strdup_printf (
			_("Could not write log entry: %s\n"
			"Further operations on this server "
			"will not be replayed when you\n"
			"reconnect to the network."),
			g_strerror (errno));
		camel_session_alert_user (
			session, CAMEL_SESSION_ALERT_ERROR, msg, NULL);
		g_free (msg);

		fclose (diary->file);
		diary->file = NULL;
	}
}

static void
free_uids (GPtrArray *array)
{
	while (array->len--)
		g_free (array->pdata[array->len]);
	g_ptr_array_free (array, TRUE);
}

static GPtrArray *
diary_decode_uids (CamelDiscoDiary *diary)
{
	GPtrArray *uids;
	gchar *uid;
	guint32 i;

	if (camel_file_util_decode_uint32 (diary->file, &i) == -1)
		return NULL;
	uids = g_ptr_array_new ();
	while (i--) {
		if (camel_file_util_decode_string (diary->file, &uid) == -1) {
			free_uids (uids);
			return NULL;
		}
		g_ptr_array_add (uids, uid);
	}

	return uids;
}

static CamelFolder *
diary_decode_folder (CamelDiscoDiary *diary,
                     GCancellable *cancellable)
{
	CamelFolder *folder;
	gchar *name;

	if (camel_file_util_decode_string (diary->file, &name) == -1)
		return NULL;
	folder = g_hash_table_lookup (diary->folders, name);
	if (!folder) {
		GError *error = NULL;
		gchar *msg;

		folder = camel_store_get_folder_sync (
			CAMEL_STORE (diary->store),
			name, 0, cancellable, &error);
		if (folder)
			g_hash_table_insert (diary->folders, name, folder);
		else {
			msg = g_strdup_printf (
				_("Could not open '%s':\n%s\n"
				"Changes made to this folder "
				"will not be resynchronized."),
				name, error->message);
			g_error_free (error);
			camel_session_alert_user (
				camel_service_get_session (CAMEL_SERVICE (diary->store)),
				CAMEL_SESSION_ALERT_WARNING,
				msg, NULL);
			g_free (msg);
			g_free (name);
		}
	} else
		g_free (name);
	return folder;
}

static void
close_folder (gchar *name,
              CamelFolder *folder,
              GCancellable *cancellable)
{
	g_free (name);
	camel_folder_synchronize_sync (folder, FALSE, cancellable, NULL);
	g_object_unref (folder);
}

void
camel_disco_diary_replay (CamelDiscoDiary *diary,
                          GCancellable *cancellable,
                          GError **error)
{
	guint32 action;
	goffset size;
	GError *local_error = NULL;

	d (printf ("disco diary replay\n"));

	fseek (diary->file, 0, SEEK_END);
	size = ftell (diary->file);
	g_return_if_fail (size != 0);
	rewind (diary->file);

	camel_operation_push_message (
		cancellable, _("Resynchronizing with server"));

	while (local_error == NULL) {
		camel_operation_progress (
			cancellable, (ftell (diary->file) / size) * 100);

		if (camel_file_util_decode_uint32 (diary->file, &action) == -1)
			break;
		if (action == CAMEL_DISCO_DIARY_END)
			break;

		switch (action) {
		case CAMEL_DISCO_DIARY_FOLDER_EXPUNGE:
		{
			CamelFolder *folder;
			GPtrArray *uids;

			folder = diary_decode_folder (diary, cancellable);
			uids = diary_decode_uids (diary);
			if (!uids)
				goto lose;

			if (folder)
				camel_disco_folder_expunge_uids (
					folder, uids, cancellable,
					&local_error);
			free_uids (uids);
			break;
		}

		case CAMEL_DISCO_DIARY_FOLDER_APPEND:
		{
			CamelFolder *folder;
			gchar *uid, *ret_uid;
			CamelMimeMessage *message;
			CamelMessageInfo *info;

			folder = diary_decode_folder (diary, cancellable);
			if (camel_file_util_decode_string (diary->file, &uid) == -1)
				goto lose;

			if (!folder) {
				g_free (uid);
				continue;
			}

			message = camel_folder_get_message_sync (
				folder, uid, cancellable, NULL);
			if (!message) {
				/* The message was appended and then deleted. */
				g_free (uid);
				continue;
			}
			info = camel_folder_get_message_info (folder, uid);

			camel_folder_append_message_sync (
				folder, message, info, &ret_uid,
				cancellable, &local_error);
			camel_folder_free_message_info (folder, info);

			if (ret_uid) {
				camel_disco_diary_uidmap_add (diary, uid, ret_uid);
				g_free (ret_uid);
			}
			g_free (uid);

			break;
		}

		case CAMEL_DISCO_DIARY_FOLDER_TRANSFER:
		{
			CamelFolder *source, *destination;
			GPtrArray *uids, *ret_uids;
			guint32 delete_originals;
			gint i;

			source = diary_decode_folder (diary, cancellable);
			destination = diary_decode_folder (diary, cancellable);
			uids = diary_decode_uids (diary);
			if (!uids)
				goto lose;
			if (camel_file_util_decode_uint32 (diary->file, &delete_originals) == -1)
				goto lose;

			if (!source || !destination) {
				free_uids (uids);
				continue;
			}

			camel_folder_transfer_messages_to_sync (
				source, uids, destination, delete_originals,
				&ret_uids, cancellable, &local_error);

			if (ret_uids) {
				for (i = 0; i < uids->len; i++) {
					if (!ret_uids->pdata[i])
						continue;
					camel_disco_diary_uidmap_add (diary, uids->pdata[i], ret_uids->pdata[i]);
					g_free (ret_uids->pdata[i]);
				}
				g_ptr_array_free (ret_uids, TRUE);
			}
			free_uids (uids);
			break;
		}

		}
	}

 lose:
	camel_operation_pop_message (cancellable);

	/* Close folders */
	g_hash_table_foreach (
		diary->folders, (GHFunc) close_folder, cancellable);
	g_hash_table_destroy (diary->folders);
	diary->folders = NULL;

	/* Truncate the log */
	ftruncate (fileno (diary->file), 0);
	rewind (diary->file);

	g_propagate_error (error, local_error);
}

CamelDiscoDiary *
camel_disco_diary_new (CamelDiscoStore *store,
                       const gchar *filename,
                       GError **error)
{
	CamelDiscoDiary *diary;

	g_return_val_if_fail (CAMEL_IS_DISCO_STORE (store), NULL);
	g_return_val_if_fail (filename != NULL, NULL);

	diary = g_object_new (CAMEL_TYPE_DISCO_DIARY, NULL);
	diary->store = store;

	d (printf ("diary log file '%s'\n", filename));

	/* Note that the linux man page says:
	 *
	 * a+     Open for reading and appending (writing at end of file).
	 *        The file is created if it does not exist.  The stream is
	 *        positioned at the end of the file.
	 *
	 * However, c99 (which glibc uses?) says:
	 * a+     append; open or create text file for update, writing at
	 *        end-of-file
	 *
	 * So we must seek ourselves.
	 */

	diary->file = g_fopen (filename, "a+b");
	if (!diary->file) {
		g_object_unref (diary);
		g_set_error (
			error, G_IO_ERROR,
			g_io_error_from_errno (errno),
			"Could not open journal file: %s",
			g_strerror (errno));
		return NULL;
	}

	fseek (diary->file, 0, SEEK_END);

	d (printf (" is at %ld\n", ftell (diary->file)));

	return diary;
}

gboolean
camel_disco_diary_empty (CamelDiscoDiary *diary)
{
	return ftell (diary->file) == 0;
}

void
camel_disco_diary_uidmap_add (CamelDiscoDiary *diary,
                              const gchar *old_uid,
                              const gchar *new_uid)
{
	g_hash_table_insert (
		diary->uidmap,
		g_strdup (old_uid),
		g_strdup (new_uid));
}

const gchar *
camel_disco_diary_uidmap_lookup (CamelDiscoDiary *diary,
                                 const gchar *uid)
{
	return g_hash_table_lookup (diary->uidmap, uid);
}
