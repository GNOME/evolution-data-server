/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8; fill-column: 160 -*-
 *
 * Authors: Michael Zucchi <notzed@ximian.com>
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This library is free software you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <glib/gi18n-lib.h>
#include <glib/gstdio.h>

#include "camel-maildir-folder.h"
#include "camel-maildir-store.h"
#include "camel-maildir-summary.h"

#define d(x) /*(printf("%s(%d): ", __FILE__, __LINE__),(x))*/

G_DEFINE_TYPE (CamelMaildirFolder, camel_maildir_folder, CAMEL_TYPE_LOCAL_FOLDER)

static gint
maildir_folder_cmp_uids (CamelFolder *folder,
                         const gchar *uid1,
                         const gchar *uid2)
{
	CamelMessageInfo *a, *b;
	time_t tma, tmb;

	g_return_val_if_fail (folder != NULL, 0);
	g_return_val_if_fail (folder->summary != NULL, 0);

	a = camel_folder_summary_get (folder->summary, uid1);
	b = camel_folder_summary_get (folder->summary, uid2);

	if (!a || !b) {
		/* It's not a problem when one of the messages is not in the summary */
		if (a)
			camel_message_info_unref (a);
		if (b)
			camel_message_info_unref (b);

		if (a == b)
			return 0;
		if (!a)
			return -1;
		return 1;
	}

	tma = camel_message_info_date_received (a);
	tmb = camel_message_info_date_received (b);

	camel_message_info_unref (a);
	camel_message_info_unref (b);

	return tma < tmb ? -1 : tma == tmb ? 0 : 1;
}

static void
maildir_folder_sort_uids (CamelFolder *folder,
                          GPtrArray *uids)
{
	g_return_if_fail (camel_maildir_folder_parent_class != NULL);
	g_return_if_fail (folder != NULL);

	if (uids && uids->len > 1)
		camel_folder_summary_prepare_fetch_all (folder->summary, NULL);

	/* Chain up to parent's sort_uids() method. */
	CAMEL_FOLDER_CLASS (camel_maildir_folder_parent_class)->sort_uids (folder, uids);
}

static gchar *
maildir_folder_get_filename (CamelFolder *folder,
                             const gchar *uid,
                             GError **error)
{
	CamelLocalFolder *lf = (CamelLocalFolder *) folder;
	CamelMaildirMessageInfo *mdi;
	CamelMessageInfo *info;
	gchar *res;

	/* get the message summary info */
	if ((info = camel_folder_summary_get (folder->summary, uid)) == NULL) {
		set_cannot_get_message_ex (
			error, CAMEL_FOLDER_ERROR_INVALID_UID,
			uid, lf->folder_path, _("No such message"));
		return NULL;
	}

	mdi = (CamelMaildirMessageInfo *) info;

	/* If filename is NULL, it means folder_summary_check is not yet executed.
	 * Try to find the file in the folder and use it, otherwise construct its
	 * name based on actual flags.
	*/
	if (!camel_maildir_info_filename (mdi)) {
		const gchar *uid = camel_message_info_uid (info);

		if (uid) {
			GDir *dir;
			gchar *dirname;

			dirname = g_strdup_printf ("%s/cur", lf->folder_path);
			dir = g_dir_open (dirname, 0, NULL);
			g_free (dirname);

			if (dir) {
				const gchar *filename;
				gint uid_len = strlen (uid);

				while (filename = g_dir_read_name (dir), filename) {
					if (g_str_has_prefix (filename, uid) && (filename[uid_len] == '\0' || filename[uid_len] == ':')) {
						camel_maildir_info_set_filename (mdi, g_strdup (filename));
						break;
					}
				}

				g_dir_close (dir);
			}
		}

		if (!camel_maildir_info_filename (mdi)) {
			camel_maildir_info_set_filename (mdi, camel_maildir_summary_info_to_name (mdi));
		}
	}

	res = g_strdup_printf ("%s/cur/%s", lf->folder_path, camel_maildir_info_filename (mdi));

	camel_message_info_unref (info);

	return res;
}

static gboolean
maildir_folder_append_message_sync (CamelFolder *folder,
                                    CamelMimeMessage *message,
                                    CamelMessageInfo *info,
                                    gchar **appended_uid,
                                    GCancellable *cancellable,
                                    GError **error)
{
	CamelLocalFolder *lf = (CamelLocalFolder *) folder;
	CamelStream *output_stream;
	CamelMessageInfo *mi;
	CamelMaildirMessageInfo *mdi;
	gchar *name, *dest = NULL;
	gboolean success = TRUE, has_attachment;

	d (printf ("Appending message\n"));

	/* If we can't lock, don't do anything */
	if (!lf || camel_local_folder_lock (lf, CAMEL_LOCK_WRITE, error) == -1)
		return FALSE;

	/* add it to the summary/assign the uid, etc */
	mi = camel_local_summary_add (
		CAMEL_LOCAL_SUMMARY (folder->summary),
		message, info, lf->changes, error);
	if (mi == NULL)
		goto check_changed;

	has_attachment = camel_mime_message_has_attachment (message);
	if (((camel_message_info_flags (mi) & CAMEL_MESSAGE_ATTACHMENTS) && !has_attachment) ||
	    ((camel_message_info_flags (mi) & CAMEL_MESSAGE_ATTACHMENTS) == 0 && has_attachment)) {
		camel_message_info_set_flags (mi, CAMEL_MESSAGE_ATTACHMENTS, has_attachment ? CAMEL_MESSAGE_ATTACHMENTS : 0);
	}

	mdi = (CamelMaildirMessageInfo *) mi;

	d (printf ("Appending message: uid is %s filename is %s\n", camel_message_info_uid (mi), mdi->filename));

	/* write it out to tmp, use the uid we got from the summary */
	name = g_strdup_printf ("%s/tmp/%s", lf->folder_path, camel_message_info_uid (mi));
	output_stream = camel_stream_fs_new_with_name (
		name, O_WRONLY | O_CREAT, 0600, error);
	if (output_stream == NULL)
		goto fail_write;

	if (camel_data_wrapper_write_to_stream_sync (
		(CamelDataWrapper *) message, output_stream, cancellable, error) == -1
	    || camel_stream_close (output_stream, cancellable, error) == -1)
		goto fail_write;

	/* now move from tmp to cur (bypass new, does it matter?) */
	dest = g_strdup_printf ("%s/cur/%s", lf->folder_path, camel_maildir_info_filename (mdi));
	if (g_rename (name, dest) == -1) {
		g_set_error (
			error, G_IO_ERROR,
			g_io_error_from_errno (errno),
			"%s", g_strerror (errno));
		goto fail_write;
	}

	g_free (dest);
	g_free (name);

	if (appended_uid)
		*appended_uid = g_strdup(camel_message_info_uid(mi));

	if (output_stream)
		g_object_unref (output_stream);

	goto check_changed;

 fail_write:

	/* remove the summary info so we are not out-of-sync with the mh folder */
	camel_folder_summary_remove (CAMEL_FOLDER_SUMMARY (folder->summary), mi);

	g_prefix_error (
		error, _("Cannot append message to maildir folder: %s: "),
		name);

	if (output_stream) {
		g_object_unref (output_stream);
		unlink (name);
	}

	g_free (name);
	g_free (dest);

	success = FALSE;

 check_changed:
	camel_local_folder_unlock (lf);

	if (camel_folder_change_info_changed (lf->changes)) {
		camel_folder_changed (folder, lf->changes);
		camel_folder_change_info_clear (lf->changes);
	}

	return success;
}

static CamelMimeMessage *
maildir_folder_get_message_sync (CamelFolder *folder,
                                 const gchar *uid,
                                 GCancellable *cancellable,
                                 GError **error)
{
	CamelLocalFolder *lf = (CamelLocalFolder *) folder;
	CamelStream *message_stream = NULL;
	CamelMimeMessage *message = NULL;
	gchar *name = NULL;

	d (printf ("getting message: %s\n", uid));

	if (!lf || camel_local_folder_lock (lf, CAMEL_LOCK_WRITE, error) == -1)
		return NULL;

	name = maildir_folder_get_filename (folder, uid, error);
	if (!name)
		goto fail;

	message_stream = camel_stream_fs_new_with_name (
		name, O_RDONLY, 0, error);
	if (message_stream == NULL) {
		g_prefix_error (
			error, _("Cannot get message %s from folder %s: "),
			uid, lf->folder_path);
		goto fail;
	}

	message = camel_mime_message_new ();
	if (!camel_data_wrapper_construct_from_stream_sync (
		(CamelDataWrapper *) message,
		message_stream, cancellable, error)) {
		g_prefix_error (
			error, _("Cannot get message %s from folder %s: "),
			uid, lf->folder_path);
		g_object_unref (message);
		message = NULL;

	}
	g_object_unref (message_stream);
 fail:
	g_free (name);

	camel_local_folder_unlock (lf);

	if (lf && camel_folder_change_info_changed (lf->changes)) {
		camel_folder_changed (folder, lf->changes);
		camel_folder_change_info_clear (lf->changes);
	}

	return message;
}

static gboolean
maildir_folder_transfer_messages_to_sync (CamelFolder *source,
                                          GPtrArray *uids,
                                          CamelFolder *dest,
                                          gboolean delete_originals,
                                          GPtrArray **transferred_uids,
                                          GCancellable *cancellable,
                                          GError **error)
{
	gboolean fallback = FALSE;

	if (delete_originals && CAMEL_IS_MAILDIR_FOLDER (source) && CAMEL_IS_MAILDIR_FOLDER (dest)
	    && camel_folder_get_parent_store (source) == camel_folder_get_parent_store (dest)) {
		gint i;
		CamelLocalFolder *lf = (CamelLocalFolder *) source;
		CamelLocalFolder *df = (CamelLocalFolder *) dest;

		camel_operation_push_message (
			cancellable, _("Moving messages"));

		camel_folder_freeze (dest);
		camel_folder_freeze (source);

		for (i = 0; i < uids->len; i++) {
			gchar *uid = (gchar *) uids->pdata[i];
			gchar *s_filename, *d_filename, *new_filename;
			CamelMaildirMessageInfo *mdi;
			CamelMessageInfo *info;

			if ((info = camel_folder_summary_get (source->summary, uid)) == NULL) {
				set_cannot_get_message_ex (
					error, CAMEL_FOLDER_ERROR_INVALID_UID,
					uid, lf->folder_path, _("No such message"));
				return FALSE;
			}

			mdi = (CamelMaildirMessageInfo *) info;
			new_filename = camel_maildir_summary_info_to_name (mdi);

			d_filename = g_strdup_printf ("%s/cur/%s", df->folder_path, new_filename);
			s_filename = g_strdup_printf ("%s/cur/%s", lf->folder_path, camel_maildir_info_filename (mdi));

			if (g_rename (s_filename, d_filename) != 0) {
				if (errno == EXDEV) {
					i = uids->len + 1;
					fallback = TRUE;
				} else {
					g_set_error (
						error, G_IO_ERROR,
						g_io_error_from_errno (errno),
						_("Cannot transfer message to destination folder: %s"),
						g_strerror (errno));
					camel_message_info_unref (info);
					g_free (s_filename);
					g_free (d_filename);
					g_free (new_filename);
					break;
				}
			} else {
				CamelMessageInfo *clone;
				CamelMaildirMessageInfo *mclone;

				clone = camel_message_info_clone (info);
				clone->summary = dest->summary;

				mclone = (CamelMaildirMessageInfo *) clone;
				/* preserve also UID, as it matches the file name */
				mclone->info.info.uid = camel_pstring_strdup (camel_message_info_uid (info));
				camel_maildir_info_set_filename (clone, g_strdup (new_filename));
				/* unset deleted flag when transferring from trash folder */
				if ((source->folder_flags & CAMEL_FOLDER_IS_TRASH) != 0)
					camel_message_info_set_flags (info, CAMEL_MESSAGE_DELETED, 0);
				/* unset junk flag when transferring from junk folder */
				if ((source->folder_flags & CAMEL_FOLDER_IS_JUNK) != 0)
					camel_message_info_set_flags (info, CAMEL_MESSAGE_JUNK, 0);
				camel_folder_summary_add (dest->summary, clone);

				camel_folder_change_info_add_uid (df->changes, camel_message_info_uid (clone));

				camel_folder_set_message_flags (
					source, uid, CAMEL_MESSAGE_DELETED |
					CAMEL_MESSAGE_SEEN, ~0);
				camel_folder_change_info_remove_uid (lf->changes, camel_message_info_uid (info));
				camel_folder_summary_remove (source->summary, info);
			}

			camel_message_info_unref (info);
			g_free (s_filename);
			g_free (d_filename);
			g_free (new_filename);
		}

		if (lf && camel_folder_change_info_changed (lf->changes)) {
			camel_folder_changed (source, lf->changes);
			camel_folder_change_info_clear (lf->changes);
		}

		if (df && camel_folder_change_info_changed (df->changes)) {
			camel_folder_changed (dest, df->changes);
			camel_folder_change_info_clear (df->changes);
		}

		camel_folder_thaw (source);
		camel_folder_thaw (dest);

		camel_operation_pop_message (cancellable);
	} else
		fallback = TRUE;

	if (fallback) {
		CamelFolderClass *folder_class;

		/* Chain up to parent's transfer_messages_to() method. */
		folder_class = CAMEL_FOLDER_CLASS (camel_maildir_folder_parent_class);
		return folder_class->transfer_messages_to_sync (
			source, uids, dest, delete_originals,
			transferred_uids, cancellable, error);
	}

	return TRUE;
}

static CamelLocalSummary *
maildir_folder_create_summary (CamelLocalFolder *lf,
                               const gchar *folder,
                               CamelIndex *index)
{
	return (CamelLocalSummary *) camel_maildir_summary_new (
		CAMEL_FOLDER (lf), folder, index);
}

static void
camel_maildir_folder_class_init (CamelMaildirFolderClass *class)
{
	CamelFolderClass *folder_class;
	CamelLocalFolderClass *local_folder_class;

	folder_class = CAMEL_FOLDER_CLASS (class);
	folder_class->cmp_uids = maildir_folder_cmp_uids;
	folder_class->sort_uids = maildir_folder_sort_uids;
	folder_class->get_filename = maildir_folder_get_filename;
	folder_class->append_message_sync = maildir_folder_append_message_sync;
	folder_class->get_message_sync = maildir_folder_get_message_sync;
	folder_class->transfer_messages_to_sync = maildir_folder_transfer_messages_to_sync;

	local_folder_class = CAMEL_LOCAL_FOLDER_CLASS (class);
	local_folder_class->create_summary = maildir_folder_create_summary;
}

static void
camel_maildir_folder_init (CamelMaildirFolder *maildir_folder)
{
}

CamelFolder *
camel_maildir_folder_new (CamelStore *parent_store,
                          const gchar *full_name,
                          guint32 flags,
                          GCancellable *cancellable,
                          GError **error)
{
	CamelFolder *folder;
	CamelService *service;
	CamelSettings *settings;
	gboolean filter_inbox;
	gchar *basename;

	d (printf ("Creating maildir folder: %s\n", full_name));

	if (g_strcmp0 (full_name, ".") == 0)
		basename = g_strdup (_("Inbox"));
	else
		basename = g_path_get_basename (full_name);

	folder = g_object_new (
		CAMEL_TYPE_MAILDIR_FOLDER,
		"display-name", basename,
		"full-name", full_name,
		"parent-store", parent_store,
		NULL);

	service = CAMEL_SERVICE (parent_store);

	settings = camel_service_ref_settings (service);

	filter_inbox = camel_store_settings_get_filter_inbox (
		CAMEL_STORE_SETTINGS (settings));

	g_object_unref (settings);

	if (filter_inbox && (g_str_equal (full_name, ".") || g_ascii_strcasecmp (full_name, "Inbox") == 0))
		folder->folder_flags |= CAMEL_FOLDER_FILTER_RECENT;

	folder = (CamelFolder *) camel_local_folder_construct (
		CAMEL_LOCAL_FOLDER (folder), flags, cancellable, error);

	g_free (basename);

	/* indexing doesn't work with maildir properly, thus disable it */
	g_object_set (folder, "index-body", FALSE, NULL);

	return folder;
}

