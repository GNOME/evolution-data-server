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

#include "camel-mh-folder.h"
#include "camel-mh-store.h"
#include "camel-mh-summary.h"

#define d(x) /*(printf("%s(%d): ", __FILE__, __LINE__),(x))*/

static CamelLocalFolderClass *parent_class = NULL;

/* Returns the class for a CamelMhFolder */
#define CMHF_CLASS(so) CAMEL_MH_FOLDER_CLASS (CAMEL_OBJECT_GET_CLASS(so))
#define CF_CLASS(so) CAMEL_FOLDER_CLASS (CAMEL_OBJECT_GET_CLASS(so))
#define CMHS_CLASS(so) CAMEL_STORE_CLASS (CAMEL_OBJECT_GET_CLASS(so))

static CamelLocalSummary *mh_create_summary(CamelLocalFolder *lf, const gchar *path, const gchar *folder, CamelIndex *index);

static void mh_append_message(CamelFolder * folder, CamelMimeMessage * message, const CamelMessageInfo *info, gchar **appended_uid, CamelException * ex);
static CamelMimeMessage *mh_get_message(CamelFolder * folder, const gchar * uid, CamelException * ex);
static gchar * mh_get_filename (CamelFolder *folder, const gchar *uid, CamelException *ex);

static void mh_finalize(CamelObject * object);

static void camel_mh_folder_class_init(CamelObjectClass * camel_mh_folder_class)
{
	CamelFolderClass *camel_folder_class = CAMEL_FOLDER_CLASS(camel_mh_folder_class);
	CamelLocalFolderClass *lclass = (CamelLocalFolderClass *)camel_mh_folder_class;

	parent_class = CAMEL_LOCAL_FOLDER_CLASS (camel_type_get_global_classfuncs(camel_local_folder_get_type()));

	/* virtual method definition */

	/* virtual method overload */
	camel_folder_class->append_message = mh_append_message;
	camel_folder_class->get_message = mh_get_message;
	camel_folder_class->get_filename = mh_get_filename;

	lclass->create_summary = mh_create_summary;
}

static void mh_init(gpointer object, gpointer klass)
{
	/*CamelFolder *folder = object;
	  CamelMhFolder *mh_folder = object;*/
}

static void mh_finalize(CamelObject * object)
{
	/*CamelMhFolder *mh_folder = CAMEL_MH_FOLDER(object);*/
}

CamelType camel_mh_folder_get_type(void)
{
	static CamelType camel_mh_folder_type = CAMEL_INVALID_TYPE;

	if (camel_mh_folder_type == CAMEL_INVALID_TYPE) {
		camel_mh_folder_type = camel_type_register(CAMEL_LOCAL_FOLDER_TYPE, "CamelMhFolder",
							   sizeof(CamelMhFolder),
							   sizeof(CamelMhFolderClass),
							   (CamelObjectClassInitFunc) camel_mh_folder_class_init,
							   NULL,
							   (CamelObjectInitFunc) mh_init,
							   (CamelObjectFinalizeFunc) mh_finalize);
	}

	return camel_mh_folder_type;
}

CamelFolder *
camel_mh_folder_new(CamelStore *parent_store, const gchar *full_name, guint32 flags, CamelException *ex)
{
	CamelFolder *folder;

	d(printf("Creating mh folder: %s\n", full_name));

	folder = (CamelFolder *)camel_object_new(CAMEL_MH_FOLDER_TYPE);
	folder = (CamelFolder *)camel_local_folder_construct((CamelLocalFolder *)folder,
							     parent_store, full_name, flags, ex);

	return folder;
}

static CamelLocalSummary *mh_create_summary(CamelLocalFolder *lf, const gchar *path, const gchar *folder, CamelIndex *index)
{
	return (CamelLocalSummary *)camel_mh_summary_new((CamelFolder *)lf, path, folder, index);
}

static void
mh_append_message (CamelFolder *folder, CamelMimeMessage *message, const CamelMessageInfo *info, gchar **appended_uid, CamelException *ex)
{
	CamelLocalFolder *lf = (CamelLocalFolder *)folder;
	CamelStream *output_stream;
	CamelMessageInfo *mi;
	gchar *name;

	/* FIXME: probably needs additional locking (although mh doesn't appear do do it) */

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

	d(printf("Appending message: uid is %s\n", camel_message_info_uid(mi)));

	/* write it out, use the uid we got from the summary */
	name = g_strdup_printf("%s/%s", lf->folder_path, camel_message_info_uid(mi));
	output_stream = camel_stream_fs_new_with_name(name, O_WRONLY|O_CREAT, 0600);
	if (output_stream == NULL)
		goto fail_write;

	if (camel_data_wrapper_write_to_stream ((CamelDataWrapper *)message, output_stream) == -1
	    || camel_stream_close (output_stream) == -1)
		goto fail_write;

	/* close this? */
	camel_object_unref (CAMEL_OBJECT (output_stream));

	g_free(name);

	if (appended_uid)
		*appended_uid = g_strdup(camel_message_info_uid(mi));

	goto check_changed;

 fail_write:

	/* remove the summary info so we are not out-of-sync with the mh folder */
	camel_folder_summary_remove_uid (CAMEL_FOLDER_SUMMARY (folder->summary),
					 camel_message_info_uid (mi));

	if (errno == EINTR)
		camel_exception_set (ex, CAMEL_EXCEPTION_USER_CANCEL,
				     _("MH append message canceled"));
	else
		camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
				      _("Cannot append message to mh folder: %s: %s"),
				      name, g_strerror (errno));

	if (output_stream) {
		camel_object_unref (CAMEL_OBJECT (output_stream));
		unlink (name);
	}

	g_free (name);

 check_changed:
	camel_local_folder_unlock (lf);

	if (lf && camel_folder_change_info_changed (lf->changes)) {
		camel_object_trigger_event (CAMEL_OBJECT (folder), "folder_changed", lf->changes);
		camel_folder_change_info_clear (lf->changes);
	}
}

static gchar * mh_get_filename (CamelFolder *folder, const gchar *uid, CamelException *ex)
{
	CamelLocalFolder *lf = (CamelLocalFolder *)folder;

	return g_strdup_printf("%s/%s", lf->folder_path, uid);
}

static CamelMimeMessage *mh_get_message(CamelFolder * folder, const gchar * uid, CamelException * ex)
{
	CamelLocalFolder *lf = (CamelLocalFolder *)folder;
	CamelStream *message_stream = NULL;
	CamelMimeMessage *message = NULL;
	CamelMessageInfo *info;
	gchar *name = NULL;

	d(printf("getting message: %s\n", uid));

	if (camel_local_folder_lock (lf, CAMEL_LOCK_WRITE, ex) == -1)
		return NULL;

	/* get the message summary info */
	if ((info = camel_folder_summary_uid(folder->summary, uid)) == NULL) {
		set_cannot_get_message_ex (ex, CAMEL_EXCEPTION_FOLDER_INVALID_UID,
				     uid, lf->folder_path, _("No such message"));
		goto fail;
	}

	/* we only need it to check the message exists */
	camel_message_info_free(info);

	name = g_strdup_printf("%s/%s", lf->folder_path, uid);
	if ((message_stream = camel_stream_fs_new_with_name(name, O_RDONLY, 0)) == NULL) {
		set_cannot_get_message_ex (ex, CAMEL_EXCEPTION_SYSTEM,
				      name, lf->folder_path, g_strerror (errno));
		goto fail;
	}

	message = camel_mime_message_new();
	if (camel_data_wrapper_construct_from_stream((CamelDataWrapper *)message, message_stream) == -1) {
		set_cannot_get_message_ex (ex, CAMEL_EXCEPTION_SYSTEM,
				      name, lf->folder_path, _("Message construction failed."));
		camel_object_unref (message);
		message = NULL;

	}
	camel_object_unref (message_stream);

 fail:
	g_free (name);

	camel_local_folder_unlock (lf);

	if (lf && camel_folder_change_info_changed (lf->changes)) {
		camel_object_trigger_event (CAMEL_OBJECT (folder), "folder_changed", lf->changes);
		camel_folder_change_info_clear (lf->changes);
	}

	return message;
}
