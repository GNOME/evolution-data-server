/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8; fill-column: 160 -*-
 *
 * Authors: Michael Zucchi <notzed@ximian.com>
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

#include "camel-data-wrapper.h"
#include "camel-exception.h"
#include "camel-mime-message.h"
#include "camel-stream-fs.h"

#include "camel-maildir-folder.h"
#include "camel-maildir-store.h"
#include "camel-maildir-summary.h"

#define d(x) /*(printf("%s(%d): ", __FILE__, __LINE__),(x))*/

static CamelLocalFolderClass *parent_class = NULL;

/* Returns the class for a CamelMaildirFolder */
#define CMAILDIRF_CLASS(so) CAMEL_MAILDIR_FOLDER_CLASS (CAMEL_OBJECT_GET_CLASS(so))
#define CF_CLASS(so) CAMEL_FOLDER_CLASS (CAMEL_OBJECT_GET_CLASS(so))
#define CMAILDIRS_CLASS(so) CAMEL_STORE_CLASS (CAMEL_OBJECT_GET_CLASS(so))

static CamelLocalSummary *maildir_create_summary(CamelLocalFolder *lf, const gchar *path, const gchar *folder, CamelIndex *index);

static void maildir_append_message(CamelFolder * folder, CamelMimeMessage * message, const CamelMessageInfo *info, gchar **appended_uid, CamelException * ex);
static CamelMimeMessage *maildir_get_message(CamelFolder * folder, const gchar * uid, CamelException * ex);
static gchar * maildir_get_filename (CamelFolder *folder, const gchar *uid, CamelException *ex);
static gint maildir_cmp_uids (CamelFolder *folder, const gchar *uid1, const gchar *uid2);
static void maildir_sort_uids (CamelFolder *folder, GPtrArray *uids);
static void maildir_transfer_messages_to (CamelFolder *source, GPtrArray *uids, CamelFolder *dest, GPtrArray **transferred_uids, gboolean delete_originals, CamelException *ex);
static void maildir_finalize(CamelObject * object);

static gint
maildir_folder_getv(CamelObject *object, CamelException *ex, CamelArgGetV *args)
{
	CamelFolder *folder = (CamelFolder *)object;
	gint i;
	guint32 tag;

	for (i=0;i<args->argc;i++) {
		CamelArgGet *arg = &args->argv[i];

		tag = arg->tag;

		switch (tag & CAMEL_ARG_TAG) {
		case CAMEL_FOLDER_ARG_NAME:
			if (!strcmp(folder->full_name, "."))
				*arg->ca_str = _("Inbox");
			else
				*arg->ca_str = folder->name;
			break;
		default:
			continue;
		}

		arg->tag = (tag & CAMEL_ARG_TYPE) | CAMEL_ARG_IGNORE;
	}

	return ((CamelObjectClass *)parent_class)->getv(object, ex, args);
}

static void camel_maildir_folder_class_init(CamelObjectClass * camel_maildir_folder_class)
{
	CamelFolderClass *camel_folder_class = CAMEL_FOLDER_CLASS(camel_maildir_folder_class);
	CamelLocalFolderClass *lclass = (CamelLocalFolderClass *)camel_maildir_folder_class;

	parent_class = CAMEL_LOCAL_FOLDER_CLASS (camel_type_get_global_classfuncs(camel_local_folder_get_type()));

	/* virtual method definition */

	/* virtual method overload */
	((CamelObjectClass *)camel_folder_class)->getv = maildir_folder_getv;

	camel_folder_class->append_message = maildir_append_message;
	camel_folder_class->get_message = maildir_get_message;
	camel_folder_class->get_filename = maildir_get_filename;
	camel_folder_class->cmp_uids = maildir_cmp_uids;
	camel_folder_class->sort_uids = maildir_sort_uids;
	camel_folder_class->transfer_messages_to = maildir_transfer_messages_to;

	lclass->create_summary = maildir_create_summary;
}

static void maildir_init(gpointer object, gpointer klass)
{
	/*CamelFolder *folder = object;
	  CamelMaildirFolder *maildir_folder = object;*/
}

static void maildir_finalize(CamelObject * object)
{
	/*CamelMaildirFolder *maildir_folder = CAMEL_MAILDIR_FOLDER(object);*/
}

CamelType camel_maildir_folder_get_type(void)
{
	static CamelType camel_maildir_folder_type = CAMEL_INVALID_TYPE;

	if (camel_maildir_folder_type == CAMEL_INVALID_TYPE) {
		camel_maildir_folder_type = camel_type_register(CAMEL_LOCAL_FOLDER_TYPE, "CamelMaildirFolder",
							   sizeof(CamelMaildirFolder),
							   sizeof(CamelMaildirFolderClass),
							   (CamelObjectClassInitFunc) camel_maildir_folder_class_init,
							   NULL,
							   (CamelObjectInitFunc) maildir_init,
							   (CamelObjectFinalizeFunc) maildir_finalize);
	}

	return camel_maildir_folder_type;
}

CamelFolder *
camel_maildir_folder_new(CamelStore *parent_store, const gchar *full_name, guint32 flags, CamelException *ex)
{
	CamelFolder *folder;

	d(printf("Creating maildir folder: %s\n", full_name));

	folder = (CamelFolder *)camel_object_new(CAMEL_MAILDIR_FOLDER_TYPE);

	if (parent_store->flags & CAMEL_STORE_FILTER_INBOX
	    && strcmp(full_name, ".") == 0)
		folder->folder_flags |= CAMEL_FOLDER_FILTER_RECENT;

	folder = (CamelFolder *)camel_local_folder_construct((CamelLocalFolder *)folder,
							     parent_store, full_name, flags, ex);

	return folder;
}

static CamelLocalSummary *maildir_create_summary(CamelLocalFolder *lf, const gchar *path, const gchar *folder, CamelIndex *index)
{
	return (CamelLocalSummary *)camel_maildir_summary_new((CamelFolder *)lf, path, folder, index);
}

static void
maildir_append_message (CamelFolder *folder, CamelMimeMessage *message, const CamelMessageInfo *info, gchar **appended_uid, CamelException *ex)
{
	CamelLocalFolder *lf = (CamelLocalFolder *)folder;
	CamelStream *output_stream;
	CamelMessageInfo *mi;
	CamelMaildirMessageInfo *mdi;
	gchar *name, *dest = NULL;

	d(printf("Appending message\n"));

	/* If we can't lock, don't do anything */
	if (camel_local_folder_lock (lf, CAMEL_LOCK_WRITE, ex) == -1)
		return;

	/* add it to the summary/assign the uid, etc */
	mi = camel_local_summary_add((CamelLocalSummary *)folder->summary, message, info, lf->changes, ex);
	if (camel_exception_is_set (ex))
		goto check_changed;

	if ((camel_message_info_flags (mi) & CAMEL_MESSAGE_ATTACHMENTS) && !camel_mime_message_has_attachment (message))
		camel_message_info_set_flags (mi, CAMEL_MESSAGE_ATTACHMENTS, 0);

	mdi = (CamelMaildirMessageInfo *)mi;

	d(printf("Appending message: uid is %s filename is %s\n", camel_message_info_uid(mi), mdi->filename));

	/* write it out to tmp, use the uid we got from the summary */
	name = g_strdup_printf ("%s/tmp/%s", lf->folder_path, camel_message_info_uid(mi));
	output_stream = camel_stream_fs_new_with_name (name, O_WRONLY|O_CREAT, 0600);
	if (output_stream == NULL)
		goto fail_write;

	if (camel_data_wrapper_write_to_stream ((CamelDataWrapper *)message, output_stream) == -1
	    || camel_stream_close (output_stream) == -1)
		goto fail_write;

	/* now move from tmp to cur (bypass new, does it matter?) */
	dest = g_strdup_printf("%s/cur/%s", lf->folder_path, camel_maildir_info_filename (mdi));
	if (rename (name, dest) == -1)
		goto fail_write;

	g_free (dest);
	g_free (name);

	if (appended_uid)
		*appended_uid = g_strdup(camel_message_info_uid(mi));

	if (output_stream)
		camel_object_unref (output_stream);

	goto check_changed;

 fail_write:

	/* remove the summary info so we are not out-of-sync with the mh folder */
	camel_folder_summary_remove_uid (CAMEL_FOLDER_SUMMARY (folder->summary),
					 camel_message_info_uid (mi));

	if (errno == EINTR)
		camel_exception_set (ex, CAMEL_EXCEPTION_USER_CANCEL,
				     _("Maildir append message canceled"));
	else
		camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
				      _("Cannot append message to maildir folder: %s: %s"),
				      name, g_strerror (errno));

	if (output_stream) {
		camel_object_unref (CAMEL_OBJECT (output_stream));
		unlink (name);
	}

	g_free (name);
	g_free (dest);

 check_changed:
	camel_local_folder_unlock (lf);

	if (lf && camel_folder_change_info_changed (lf->changes)) {
		camel_object_trigger_event (CAMEL_OBJECT (folder), "folder_changed", lf->changes);
		camel_folder_change_info_clear (lf->changes);
	}
}

static gchar *
maildir_get_filename (CamelFolder *folder, const gchar *uid, CamelException *ex)
{
	CamelLocalFolder *lf = (CamelLocalFolder *)folder;
	CamelMaildirMessageInfo *mdi;
	CamelMessageInfo *info;

	/* get the message summary info */
	if ((info = camel_folder_summary_uid(folder->summary, uid)) == NULL) {
		camel_exception_setv(ex, CAMEL_EXCEPTION_FOLDER_INVALID_UID,
				     _("Cannot get message: %s from folder %s\n  %s"),
				     uid, lf->folder_path, _("No such message"));
		return NULL;
	}

	mdi = (CamelMaildirMessageInfo *)info;

	/* what do we do if the message flags (and :info data) changes?  filename mismatch - need to recheck I guess */
	return g_strdup_printf("%s/cur/%s", lf->folder_path, camel_maildir_info_filename(mdi));
}

static CamelMimeMessage *
maildir_get_message(CamelFolder * folder, const gchar * uid, CamelException * ex)
{
	CamelLocalFolder *lf = (CamelLocalFolder *)folder;
	CamelStream *message_stream = NULL;
	CamelMimeMessage *message = NULL;
	CamelMessageInfo *info;
	gchar *name = NULL;
	CamelMaildirMessageInfo *mdi;

	d(printf("getting message: %s\n", uid));

	if (camel_local_folder_lock (lf, CAMEL_LOCK_WRITE, ex) == -1)
		return NULL;

	/* get the message summary info */
	if ((info = camel_folder_summary_uid(folder->summary, uid)) == NULL) {
		camel_exception_setv(ex, CAMEL_EXCEPTION_FOLDER_INVALID_UID,
				     _("Cannot get message: %s from folder %s\n  %s"),
				     uid, lf->folder_path, _("No such message"));
		goto fail;
	}

	mdi = (CamelMaildirMessageInfo *)info;

	/* what do we do if the message flags (and :info data) changes?  filename mismatch - need to recheck I guess */
	name = g_strdup_printf("%s/cur/%s", lf->folder_path, camel_maildir_info_filename(mdi));

	camel_message_info_free(info);

	if ((message_stream = camel_stream_fs_new_with_name(name, O_RDONLY, 0)) == NULL) {
		camel_exception_setv(ex, CAMEL_EXCEPTION_SYSTEM,
				     _("Cannot get message: %s from folder %s\n  %s"),
				     uid, lf->folder_path, g_strerror(errno));
		goto fail;
	}

	message = camel_mime_message_new();
	if (camel_data_wrapper_construct_from_stream((CamelDataWrapper *)message, message_stream) == -1) {
		camel_exception_setv(ex, (errno==EINTR)?CAMEL_EXCEPTION_USER_CANCEL:CAMEL_EXCEPTION_SYSTEM,
				     _("Cannot get message: %s from folder %s\n  %s"),
				     uid, lf->folder_path, _("Invalid message contents"));
		camel_object_unref((CamelObject *)message);
		message = NULL;

	}
	camel_object_unref((CamelObject *)message_stream);
 fail:
	g_free (name);

	camel_local_folder_unlock (lf);

	if (lf && camel_folder_change_info_changed (lf->changes)) {
		camel_object_trigger_event (CAMEL_OBJECT (folder), "folder_changed", lf->changes);
		camel_folder_change_info_clear (lf->changes);
	}

	return message;
}

static gint
maildir_cmp_uids (CamelFolder *folder, const gchar *uid1, const gchar *uid2)
{
	CamelMessageInfo *a, *b;
	time_t tma, tmb;

	g_return_val_if_fail (folder != NULL, 0);
	g_return_val_if_fail (folder->summary != NULL, 0);

	a = camel_folder_summary_uid (folder->summary, uid1);
	b = camel_folder_summary_uid (folder->summary, uid2);

	g_return_val_if_fail (a != NULL, 0);
	g_return_val_if_fail (b != NULL, 0);

	tma = camel_message_info_date_received (a);
	tmb = camel_message_info_date_received (b);

	return tma < tmb ? -1 : tma == tmb ? 0 : 1;
}

static void
maildir_sort_uids (CamelFolder *folder, GPtrArray *uids)
{
	g_return_if_fail (parent_class != NULL);
	g_return_if_fail (folder != NULL);

	if (uids && uids->len > 1) {
		CamelException ex;

		camel_exception_init (&ex);

		camel_folder_summary_ensure_infos_loaded (folder->summary, uids->len, &ex);

		if (camel_exception_is_set (&ex))
			g_warning ("%s: %s", G_STRFUNC, camel_exception_get_description (&ex));

		camel_exception_clear (&ex);
	}

	CAMEL_FOLDER_CLASS (parent_class)->sort_uids (folder, uids);
}

static void
maildir_transfer_messages_to (CamelFolder *source, GPtrArray *uids, CamelFolder *dest, GPtrArray **transferred_uids, gboolean delete_originals, CamelException *ex)
{
	gboolean fallback = FALSE;

	if (delete_originals && CAMEL_IS_MAILDIR_FOLDER (source) && CAMEL_IS_MAILDIR_FOLDER (dest)) {
		gint i;
		CamelLocalFolder *lf = (CamelLocalFolder *) source;
		CamelLocalFolder *df = (CamelLocalFolder *) dest;

		camel_operation_start(NULL, _("Moving messages"));

		camel_folder_freeze (dest);
		camel_folder_freeze (source);

		for (i = 0; i < uids->len; i++) {
			gchar *uid = (gchar *) uids->pdata[i];
			gchar *s_filename, *d_filename, *tmp;
			CamelMaildirMessageInfo *mdi;
			CamelMessageInfo *info;

			if ((info = camel_folder_summary_uid (source->summary, uid)) == NULL) {
				camel_exception_setv (ex, CAMEL_EXCEPTION_FOLDER_INVALID_UID,
						     _("Cannot get message: %s from folder %s\n  %s"),
						     uid, lf->folder_path, _("No such message"));
				return;
			}

			mdi = (CamelMaildirMessageInfo *) info;
			tmp = camel_maildir_summary_info_to_name (mdi);

			d_filename = g_strdup_printf ("%s/cur/%s", df->folder_path, tmp);
			g_free (tmp);
			s_filename = g_strdup_printf("%s/cur/%s", lf->folder_path, camel_maildir_info_filename (mdi));

			if (g_rename (s_filename, d_filename) != 0) {
				if (errno == EXDEV) {
					i = uids->len + 1;
					fallback = TRUE;
				} else {
					camel_exception_set (ex, CAMEL_EXCEPTION_SYSTEM,
							_("Cannot transfer message to destination folder"));
					camel_message_info_free (info);
					break;
				}
			} else {
				camel_folder_set_message_flags (source, uid, CAMEL_MESSAGE_DELETED | CAMEL_MESSAGE_SEEN, ~0);
				camel_folder_summary_remove (source->summary, info);
			}

			camel_message_info_free (info);
			g_free (s_filename);
			g_free (d_filename);
		}

		camel_folder_thaw (source);
		camel_folder_thaw (dest);

		camel_operation_end (NULL);
	} else
		fallback = TRUE;

	if (fallback)
		((CamelFolderClass *)parent_class)->transfer_messages_to (source, uids, dest, transferred_uids, delete_originals, ex);
}
