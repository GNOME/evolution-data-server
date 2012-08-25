/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8; -*- */
/* camel-imap-wrapper.c: data wrapper for offline IMAP data */

/*
 * Author: Dan Winship <danw@ximian.com>
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
#include <string.h>

#include "camel-imap-folder.h"
#include "camel-imap-wrapper.h"

#define CAMEL_IMAP_WRAPPER_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), CAMEL_TYPE_IMAP_WRAPPER, CamelImapWrapperPrivate))

struct _CamelImapWrapperPrivate {
	GMutex *lock;
};

#define CAMEL_IMAP_WRAPPER_LOCK(f, l) \
	(g_mutex_lock (((CamelImapWrapper *) f)->priv->l))
#define CAMEL_IMAP_WRAPPER_UNLOCK(f, l) \
	(g_mutex_unlock (((CamelImapWrapper *) f)->priv->l))

G_DEFINE_TYPE (CamelImapWrapper, camel_imap_wrapper, CAMEL_TYPE_DATA_WRAPPER)

static gboolean
imap_wrapper_hydrate (CamelImapWrapper *imap_wrapper,
                      CamelStream *stream,
                      GCancellable *cancellable,
                      GError **error)
{
	CamelDataWrapper *data_wrapper = (CamelDataWrapper *) imap_wrapper;
	gboolean success;

	success = camel_data_wrapper_construct_from_stream_sync (
		data_wrapper, stream, cancellable, error);

	if (success) {
		data_wrapper->offline = FALSE;

		g_object_unref (imap_wrapper->folder);
		imap_wrapper->folder = NULL;
		g_free (imap_wrapper->uid);
		imap_wrapper->uid = NULL;
		g_free (imap_wrapper->part_spec);
		imap_wrapper->part_spec = NULL;
	}

	return success;
}

static void
imap_wrapper_dispose (GObject *object)
{
	CamelImapWrapper *imap_wrapper = CAMEL_IMAP_WRAPPER (object);

	if (imap_wrapper->folder != NULL) {
		g_object_unref (imap_wrapper->folder);
		imap_wrapper->folder = NULL;
	}

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (camel_imap_wrapper_parent_class)->dispose (object);
}

static void
imap_wrapper_finalize (GObject *object)
{
	CamelImapWrapper *imap_wrapper = CAMEL_IMAP_WRAPPER (object);

	g_free (imap_wrapper->uid);
	g_free (imap_wrapper->part_spec);

	g_mutex_free (imap_wrapper->priv->lock);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (camel_imap_wrapper_parent_class)->finalize (object);
}

static gssize
imap_wrapper_write_to_stream_sync (CamelDataWrapper *data_wrapper,
                                   CamelStream *stream,
                                   GCancellable *cancellable,
                                   GError **error)
{
	CamelImapWrapper *imap_wrapper = CAMEL_IMAP_WRAPPER (data_wrapper);
	gboolean success = TRUE;

	CAMEL_IMAP_WRAPPER_LOCK (imap_wrapper, lock);
	if (data_wrapper->offline) {
		CamelStream *datastream;

		datastream = camel_imap_folder_fetch_data (
			imap_wrapper->folder, imap_wrapper->uid,
			imap_wrapper->part_spec, FALSE, cancellable, error);

		if (!datastream) {
			CAMEL_IMAP_WRAPPER_UNLOCK (imap_wrapper, lock);
			return -1;
		}

		success = imap_wrapper_hydrate (
			imap_wrapper, datastream, cancellable, error);

		g_object_unref (datastream);
	}
	CAMEL_IMAP_WRAPPER_UNLOCK (imap_wrapper, lock);

	if (!success)
		return -1;

	return CAMEL_DATA_WRAPPER_CLASS (camel_imap_wrapper_parent_class)->
		write_to_stream_sync (data_wrapper, stream, cancellable, error);
}

static void
camel_imap_wrapper_class_init (CamelImapWrapperClass *class)
{
	GObjectClass *object_class;
	CamelDataWrapperClass *data_wrapper_class;

	g_type_class_add_private (class, sizeof (CamelImapWrapperPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->dispose = imap_wrapper_dispose;
	object_class->finalize = imap_wrapper_finalize;

	data_wrapper_class = CAMEL_DATA_WRAPPER_CLASS (class);
	data_wrapper_class->write_to_stream_sync = imap_wrapper_write_to_stream_sync;
}

static void
camel_imap_wrapper_init (CamelImapWrapper *imap_wrapper)
{
	imap_wrapper->priv = CAMEL_IMAP_WRAPPER_GET_PRIVATE (imap_wrapper);
	imap_wrapper->priv->lock = g_mutex_new ();
}

CamelDataWrapper *
camel_imap_wrapper_new (CamelImapFolder *imap_folder,
                        CamelContentType *type,
                        CamelTransferEncoding encoding,
                        const gchar *uid,
                        const gchar *part_spec,
                        CamelMimePart *part)
{
	CamelImapWrapper *imap_wrapper;
	CamelStore *store;
	CamelStream *stream;
	CamelService *service;
	CamelSettings *settings;
	gboolean sync_offline = FALSE;

	store = camel_folder_get_parent_store (CAMEL_FOLDER (imap_folder));

	service = CAMEL_SERVICE (store);

	settings = camel_service_ref_settings (service);

	sync_offline =
		camel_offline_settings_get_stay_synchronized (
			CAMEL_OFFLINE_SETTINGS (settings)) ||
		camel_offline_folder_get_offline_sync (
			CAMEL_OFFLINE_FOLDER (imap_folder));

	g_object_unref (settings);

	imap_wrapper = g_object_new (CAMEL_TYPE_IMAP_WRAPPER, NULL);
	camel_data_wrapper_set_mime_type_field (CAMEL_DATA_WRAPPER (imap_wrapper), type);
	((CamelDataWrapper *) imap_wrapper)->offline = !sync_offline;
	((CamelDataWrapper *) imap_wrapper)->encoding = encoding;

	imap_wrapper->folder = g_object_ref (imap_folder);
	imap_wrapper->uid = g_strdup (uid);
	imap_wrapper->part_spec = g_strdup (part_spec);

	/* Don't ref this, it's our parent. */
	imap_wrapper->part = part;

	/* Download the attachments if sync_offline is
	 * set, else skip them by checking only in cache. */
	stream = camel_imap_folder_fetch_data (
		imap_folder, uid, part_spec, !sync_offline, NULL, NULL);

	if (stream) {
		imap_wrapper_hydrate (imap_wrapper, stream, NULL, NULL);
		g_object_unref (stream);
	}

	return (CamelDataWrapper *) imap_wrapper;
}
