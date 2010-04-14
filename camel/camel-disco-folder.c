/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-disco-folder.c: abstract class for a disconnectable folder */

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

#include <glib/gi18n-lib.h>

#include "camel-disco-folder.h"
#include "camel-disco-store.h"
#include "camel-exception.h"
#include "camel-session.h"

static CamelFolderClass *parent_class = NULL;
static GSList *disco_folder_properties;

static CamelProperty disco_property_list[] = {
	{ CAMEL_DISCO_FOLDER_OFFLINE_SYNC, "offline_sync", N_("Copy folder content locally for offline operation") },
};

/* Forward Declarations */
static void disco_expunge (CamelFolder *folder, CamelException *ex);

struct _cdf_sync_msg {
	CamelSessionThreadMsg msg;

	CamelFolder *folder;
	CamelFolderChangeInfo *changes;
};

static void
cdf_sync_offline(CamelSession *session, CamelSessionThreadMsg *mm)
{
	struct _cdf_sync_msg *m = (struct _cdf_sync_msg *)mm;
	gint i;

	camel_operation_start(NULL, _("Downloading new messages for offline mode"));

	if (m->changes) {
		for (i=0;i<m->changes->uid_added->len;i++) {
			gint pc = i * 100 / m->changes->uid_added->len;

			camel_operation_progress(NULL, pc);
			camel_disco_folder_cache_message((CamelDiscoFolder *)m->folder,
							 m->changes->uid_added->pdata[i],
							 &mm->ex);
		}
	} else {
		camel_disco_folder_prepare_for_offline((CamelDiscoFolder *)m->folder,
						       "(match-all)",
						       &mm->ex);
	}

	camel_operation_end(NULL);
}

static void
cdf_sync_free(CamelSession *session, CamelSessionThreadMsg *mm)
{
	struct _cdf_sync_msg *m = (struct _cdf_sync_msg *)mm;

	if (m->changes)
		camel_folder_change_info_free(m->changes);
	camel_object_unref (m->folder);
}

static CamelSessionThreadOps cdf_sync_ops = {
	cdf_sync_offline,
	cdf_sync_free,
};

static void
cdf_folder_changed(CamelFolder *folder, CamelFolderChangeInfo *changes, gpointer dummy)
{
	if (changes->uid_added->len > 0
	    && (((CamelDiscoFolder *)folder)->offline_sync
		|| camel_url_get_param(((CamelService *)folder->parent_store)->url, "offline_sync"))) {
		CamelSession *session = ((CamelService *)folder->parent_store)->session;
		struct _cdf_sync_msg *m;

		m = camel_session_thread_msg_new(session, &cdf_sync_ops, sizeof(*m));
		m->changes = camel_folder_change_info_new();
		camel_folder_change_info_cat(m->changes, changes);
		m->folder = camel_object_ref (folder);
		camel_session_thread_queue(session, &m->msg, 0);
	}
}

static gint
disco_getv (CamelObject *object,
            CamelException *ex,
            CamelArgGetV *args)
{
	gint i, count=0;
	guint32 tag;

	for (i=0;i<args->argc;i++) {
		CamelArgGet *arg = &args->argv[i];

		tag = arg->tag;

		switch (tag & CAMEL_ARG_TAG) {
		case CAMEL_OBJECT_ARG_PERSISTENT_PROPERTIES:
		case CAMEL_FOLDER_ARG_PROPERTIES: {
			CamelArgGetV props;

			props.argc = 1;
			props.argv[0] = *arg;
			((CamelObjectClass *)parent_class)->getv(object, ex, &props);
			*arg->ca_ptr = g_slist_concat(*arg->ca_ptr, g_slist_copy(disco_folder_properties));
			break; }
			/* disco args */
		case CAMEL_DISCO_FOLDER_ARG_OFFLINE_SYNC:
			*arg->ca_int = ((CamelDiscoFolder *)object)->offline_sync;
			break;
		default:
			count++;
			continue;
		}

		arg->tag = (tag & CAMEL_ARG_TYPE) | CAMEL_ARG_IGNORE;
	}

	if (count)
		return ((CamelObjectClass *)parent_class)->getv(object, ex, args);

	return 0;
}

static gint
disco_setv (CamelObject *object,
            CamelException *ex,
            CamelArgV *args)
{
	gint save = 0;
	gint i;
	guint32 tag;

	for (i=0;i<args->argc;i++) {
		CamelArg *arg = &args->argv[i];

		tag = arg->tag;

		switch (tag & CAMEL_ARG_TAG) {
		case CAMEL_DISCO_FOLDER_ARG_OFFLINE_SYNC:
			if (((CamelDiscoFolder *)object)->offline_sync != arg->ca_int) {
				((CamelDiscoFolder *)object)->offline_sync = arg->ca_int;
				save = 1;
			}
			break;
		default:
			continue;
		}

		arg->tag = (tag & CAMEL_ARG_TYPE) | CAMEL_ARG_IGNORE;
	}

	if (save)
		camel_object_state_write(object);

	return ((CamelObjectClass *)parent_class)->setv(object, ex, args);
}

static void
disco_refresh_info (CamelFolder *folder,
                    CamelException *ex)
{
	CamelDiscoFolderClass *disco_folder_class;

	if (camel_disco_store_status (CAMEL_DISCO_STORE (folder->parent_store)) != CAMEL_DISCO_STORE_ONLINE)
		return;

	disco_folder_class = CAMEL_DISCO_FOLDER_GET_CLASS (folder);

	disco_folder_class->refresh_info_online (folder, ex);
}

static void
disco_sync (CamelFolder *folder,
            gboolean expunge,
            CamelException *ex)
{
	CamelDiscoFolderClass *disco_folder_class;

	if (expunge) {
		disco_expunge (folder, ex);
		if (camel_exception_is_set (ex))
			return;
	}

	camel_object_state_write(folder);

	disco_folder_class = CAMEL_DISCO_FOLDER_GET_CLASS (folder);

	switch (camel_disco_store_status (CAMEL_DISCO_STORE (folder->parent_store))) {
	case CAMEL_DISCO_STORE_ONLINE:
		disco_folder_class->sync_online (folder, ex);
		return;

	case CAMEL_DISCO_STORE_OFFLINE:
		disco_folder_class->sync_offline (folder, ex);
		return;

	case CAMEL_DISCO_STORE_RESYNCING:
		disco_folder_class->sync_resyncing (folder, ex);
		return;
	}

	g_warn_if_reached ();
}

static void
disco_expunge_uids (CamelFolder *folder,
                    GPtrArray *uids,
                    CamelException *ex)
{
	CamelDiscoStore *disco = CAMEL_DISCO_STORE (folder->parent_store);
	CamelDiscoFolderClass *disco_folder_class;

	if (uids->len == 0)
		return;

	disco_folder_class = CAMEL_DISCO_FOLDER_GET_CLASS (folder);

	switch (camel_disco_store_status (disco)) {
	case CAMEL_DISCO_STORE_ONLINE:
		disco_folder_class->expunge_uids_online (
			folder, uids, ex);
		return;

	case CAMEL_DISCO_STORE_OFFLINE:
		disco_folder_class->expunge_uids_offline (
			folder, uids, ex);
		return;

	case CAMEL_DISCO_STORE_RESYNCING:
		disco_folder_class->expunge_uids_resyncing (
			folder, uids, ex);
		return;
	}

	g_warn_if_reached ();
}

static void
disco_expunge (CamelFolder *folder,
               CamelException *ex)
{
	GPtrArray *uids;
	gint i;
	guint count;
	CamelMessageInfo *info;

	uids = g_ptr_array_new ();
	count = camel_folder_summary_count (folder->summary);
	for (i = 0; i < count; i++) {
		info = camel_folder_summary_index (folder->summary, i);
		if (camel_message_info_flags(info) & CAMEL_MESSAGE_DELETED)
			g_ptr_array_add (uids, g_strdup (camel_message_info_uid (info)));
		camel_message_info_free(info);
	}

	disco_expunge_uids (folder, uids, ex);

	for (i = 0; i < uids->len; i++)
		g_free (uids->pdata[i]);
	g_ptr_array_free (uids, TRUE);
}

static void
disco_append_message (CamelFolder *folder,
                      CamelMimeMessage *message,
                      const CamelMessageInfo *info,
                      gchar **appended_uid,
                      CamelException *ex)
{
	CamelDiscoStore *disco = CAMEL_DISCO_STORE (folder->parent_store);
	CamelDiscoFolderClass *disco_folder_class;

	disco_folder_class = CAMEL_DISCO_FOLDER_GET_CLASS (folder);

	switch (camel_disco_store_status (disco)) {
	case CAMEL_DISCO_STORE_ONLINE:
		disco_folder_class->append_online (
			folder, message, info, appended_uid, ex);
		return;

	case CAMEL_DISCO_STORE_OFFLINE:
		disco_folder_class->append_offline (
			folder, message, info, appended_uid, ex);
		return;

	case CAMEL_DISCO_STORE_RESYNCING:
		disco_folder_class->append_resyncing (
			folder, message, info, appended_uid, ex);
		return;
	}

	g_warn_if_reached ();
}

static void
disco_transfer_messages_to (CamelFolder *source,
                            GPtrArray *uids,
                            CamelFolder *dest,
                            GPtrArray **transferred_uids,
                            gboolean delete_originals,
                            CamelException *ex)
{
	CamelDiscoStore *disco = CAMEL_DISCO_STORE (source->parent_store);
	CamelDiscoFolderClass *disco_folder_class;

	disco_folder_class = CAMEL_DISCO_FOLDER_GET_CLASS (source);

	switch (camel_disco_store_status (disco)) {
	case CAMEL_DISCO_STORE_ONLINE:
		disco_folder_class->transfer_online (
			source, uids, dest, transferred_uids,
			delete_originals, ex);
		return;

	case CAMEL_DISCO_STORE_OFFLINE:
		disco_folder_class->transfer_offline (
			source, uids, dest, transferred_uids,
			delete_originals, ex);
		return;

	case CAMEL_DISCO_STORE_RESYNCING:
		disco_folder_class->transfer_resyncing (
			source, uids, dest, transferred_uids,
			delete_originals, ex);
		return;
	}

	g_warn_if_reached ();
}

static void
disco_prepare_for_offline (CamelDiscoFolder *disco_folder,
                           const gchar *expression,
                           CamelException *ex)
{
	CamelFolder *folder = CAMEL_FOLDER (disco_folder);
	GPtrArray *uids;
	gint i;

	camel_operation_start(NULL, _("Preparing folder '%s' for offline"), folder->full_name);

	if (expression)
		uids = camel_folder_search_by_expression (folder, expression, ex);
	else
		uids = camel_folder_get_uids (folder);

	if (!uids) {
		camel_operation_end(NULL);
		return;
	}

	for (i = 0; i < uids->len; i++) {
		gint pc = i * 100 / uids->len;

		camel_disco_folder_cache_message (disco_folder, uids->pdata[i], ex);
		camel_operation_progress(NULL, pc);
		if (camel_exception_is_set (ex))
			break;
	}

	if (expression)
		camel_folder_search_free (folder, uids);
	else
		camel_folder_free_uids (folder, uids);

	camel_operation_end(NULL);
}

static void
disco_refresh_info_online (CamelFolder *folder,
                           CamelException *ex)
{
	/* NOOP */;
}

static void
camel_disco_folder_class_init (CamelDiscoFolderClass *class)
{
	CamelObjectClass *camel_object_class;
	CamelFolderClass *folder_class;
	gint ii;

	parent_class = CAMEL_FOLDER_CLASS (camel_type_get_global_classfuncs (camel_folder_get_type ()));

	camel_object_class = CAMEL_OBJECT_CLASS (class);
	camel_object_class->getv = disco_getv;
	camel_object_class->setv = disco_setv;

	folder_class = CAMEL_FOLDER_CLASS (class);
	folder_class->refresh_info = disco_refresh_info;
	folder_class->sync = disco_sync;
	folder_class->expunge = disco_expunge;
	folder_class->append_message = disco_append_message;
	folder_class->transfer_messages_to = disco_transfer_messages_to;

	class->prepare_for_offline = disco_prepare_for_offline;
	class->refresh_info_online = disco_refresh_info_online;

	for (ii = 0; ii < G_N_ELEMENTS (disco_property_list); ii++) {
		disco_property_list[ii].description =
			_(disco_property_list[ii].description);
		disco_folder_properties = g_slist_prepend (
			disco_folder_properties, &disco_property_list[ii]);
	}
}

static void
camel_disco_folder_init (CamelDiscoFolder *disco_folder)
{
	camel_object_hook_event (
		disco_folder, "folder_changed",
		(CamelObjectEventHookFunc) cdf_folder_changed, NULL);
}

CamelType
camel_disco_folder_get_type (void)
{
	static CamelType camel_disco_folder_type = CAMEL_INVALID_TYPE;

	if (camel_disco_folder_type == CAMEL_INVALID_TYPE) {
		camel_disco_folder_type = camel_type_register (
			CAMEL_FOLDER_TYPE, "CamelDiscoFolder",
			sizeof (CamelDiscoFolder),
			sizeof (CamelDiscoFolderClass),
			(CamelObjectClassInitFunc)camel_disco_folder_class_init, NULL,
			(CamelObjectInitFunc)camel_disco_folder_init, NULL);
	}

	return camel_disco_folder_type;
}

/**
 * camel_disco_folder_expunge_uids:
 * @folder: a (disconnectable) folder
 * @uids: array of UIDs to expunge
 * @ex: a CamelException
 *
 * This expunges the messages in @uids from @folder. It should take
 * whatever steps are needed to avoid expunging any other messages,
 * although in some cases it may not be possible to avoid expunging
 * messages that are marked deleted by another client at the same time
 * as the expunge_uids call is running.
 **/
void
camel_disco_folder_expunge_uids (CamelFolder *folder,
                                 GPtrArray *uids,
                                 CamelException *ex)
{
	g_return_if_fail (CAMEL_IS_DISCO_FOLDER (folder));
	g_return_if_fail (uids != NULL);

	disco_expunge_uids (folder, uids, ex);
}

/**
 * camel_disco_folder_cache_message:
 * @disco_folder: the folder
 * @uid: the UID of the message to cache
 * @ex: a CamelException
 *
 * Requests that @disco_folder cache message @uid to disk.
 **/
void
camel_disco_folder_cache_message (CamelDiscoFolder *disco_folder,
                                  const gchar *uid,
                                  CamelException *ex)
{
	CamelDiscoFolderClass *class;

	g_return_if_fail (CAMEL_IS_DISCO_FOLDER (disco_folder));
	g_return_if_fail (uid != NULL);

	class = CAMEL_DISCO_FOLDER_GET_CLASS (disco_folder);
	g_return_if_fail (class->cache_message != NULL);

	class->cache_message (disco_folder, uid, ex);
}

/**
 * camel_disco_folder_prepare_for_offline:
 * @disco_folder: the folder
 * @expression: an expression describing messages to synchronize, or %NULL
 * if all messages should be sync'ed.
 * @ex: a CamelException
 *
 * This prepares @disco_folder for offline operation, by downloading
 * the bodies of all messages described by @expression (using the
 * same syntax as camel_folder_search_by_expression() ).
 **/
void
camel_disco_folder_prepare_for_offline (CamelDiscoFolder *disco_folder,
                                        const gchar *expression,
                                        CamelException *ex)
{
	CamelDiscoFolderClass *class;

	g_return_if_fail (CAMEL_IS_DISCO_FOLDER (disco_folder));

	class = CAMEL_DISCO_FOLDER_GET_CLASS (disco_folder);
	g_return_if_fail (class->prepare_for_offline != NULL);

	class->prepare_for_offline (disco_folder, expression, ex);
}
