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

#define CAMEL_IMAP_WRAPPER_LOCK(f, l) (g_mutex_lock(((CamelImapWrapper *)f)->priv->l))
#define CAMEL_IMAP_WRAPPER_UNLOCK(f, l) (g_mutex_unlock(((CamelImapWrapper *)f)->priv->l))

G_DEFINE_TYPE (CamelImapWrapper, camel_imap_wrapper, CAMEL_TYPE_DATA_WRAPPER)

static void
imap_wrapper_hydrate (CamelImapWrapper *imap_wrapper,
                      CamelStream *stream)
{
	CamelDataWrapper *data_wrapper = (CamelDataWrapper *) imap_wrapper;

	data_wrapper->stream = g_object_ref (stream);
	data_wrapper->offline = FALSE;

	g_object_unref (imap_wrapper->folder);
	imap_wrapper->folder = NULL;
	g_free (imap_wrapper->uid);
	imap_wrapper->uid = NULL;
	g_free (imap_wrapper->part_spec);
	imap_wrapper->part_spec = NULL;
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
imap_wrapper_write_to_stream (CamelDataWrapper *data_wrapper,
                              CamelStream *stream,
                              GError **error)
{
	CamelImapWrapper *imap_wrapper = CAMEL_IMAP_WRAPPER (data_wrapper);

	CAMEL_IMAP_WRAPPER_LOCK (imap_wrapper, lock);
	if (data_wrapper->offline) {
		CamelStream *datastream;

		datastream = camel_imap_folder_fetch_data (
			imap_wrapper->folder, imap_wrapper->uid,
			imap_wrapper->part_spec, FALSE, NULL);

		if (!datastream) {
			CAMEL_IMAP_WRAPPER_UNLOCK (imap_wrapper, lock);
#ifdef ENETUNREACH
			errno = ENETUNREACH;
#else
/* FIXME[disk-summary] what errno to use if no ENETUNREACH */
			errno = EINVAL;
#endif
			g_set_error (
				error, G_IO_ERROR,
				g_io_error_from_errno (errno),
				"%s", g_strerror (errno));
			return -1;
		}

		imap_wrapper_hydrate (imap_wrapper, datastream);
		g_object_unref (datastream);
	}
	CAMEL_IMAP_WRAPPER_UNLOCK (imap_wrapper, lock);

	return CAMEL_DATA_WRAPPER_CLASS (camel_imap_wrapper_parent_class)->
		write_to_stream (data_wrapper, stream, error);
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
	data_wrapper_class->write_to_stream = imap_wrapper_write_to_stream;
}

static void
camel_imap_wrapper_init (CamelImapWrapper *imap_wrapper)
{
	imap_wrapper->priv = CAMEL_IMAP_WRAPPER_GET_PRIVATE (imap_wrapper);
	imap_wrapper->priv->lock = g_mutex_new ();
}

CamelDataWrapper *
camel_imap_wrapper_new (CamelImapFolder *imap_folder,
			CamelContentType *type, CamelTransferEncoding encoding,
			const gchar *uid, const gchar *part_spec,
			CamelMimePart *part)
{
	CamelImapWrapper *imap_wrapper;
	CamelStore *store;
	CamelStream *stream;
	gboolean sync_offline = FALSE;

	store = camel_folder_get_parent_store (CAMEL_FOLDER (imap_folder));
	sync_offline =
		camel_url_get_param (((CamelService *) store)->url, "sync_offline") != NULL ||
		camel_offline_folder_get_offline_sync (CAMEL_OFFLINE_FOLDER (imap_folder));

	imap_wrapper = g_object_new (CAMEL_TYPE_IMAP_WRAPPER, NULL);
	camel_data_wrapper_set_mime_type_field (CAMEL_DATA_WRAPPER (imap_wrapper), type);
	((CamelDataWrapper *)imap_wrapper)->offline = !sync_offline;
	((CamelDataWrapper *)imap_wrapper)->encoding = encoding;

	imap_wrapper->folder = g_object_ref (imap_folder);
	imap_wrapper->uid = g_strdup (uid);
	imap_wrapper->part_spec = g_strdup (part_spec);

	/* Don't ref this, it's our parent. */
	imap_wrapper->part = part;

	/* Download the attachments if sync_offline is set, else skip them by checking only in cache */
	stream = camel_imap_folder_fetch_data (imap_folder, uid, part_spec,
			!sync_offline, NULL);

	if (stream) {
		imap_wrapper_hydrate (imap_wrapper, stream);
		g_object_unref (stream);
	}

	return (CamelDataWrapper *)imap_wrapper;
}
