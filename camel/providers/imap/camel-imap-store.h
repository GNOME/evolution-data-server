/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-imap-store.h : class for an imap store */

/*
 * Authors: Jeffrey Stedfast <fejj@ximian.com>
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

#ifndef CAMEL_IMAP_STORE_H
#define CAMEL_IMAP_STORE_H

#include <sys/time.h>
#include <camel/camel.h>

/* Standard GObject macros */
#define CAMEL_TYPE_IMAP_STORE \
	(camel_imap_store_get_type ())
#define CAMEL_IMAP_STORE(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_IMAP_STORE, CamelImapStore))
#define CAMEL_IMAP_STORE_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_IMAP_STORE, CamelImapStoreClass))
#define CAMEL_IS_IMAP_STORE(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_IMAP_STORE))
#define CAMEL_IS_IMAP_STORE_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_IMAP_STORE))
#define CAMEL_IMAP_STORE_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_IMAP_STORE, CamelImapStoreClass))

G_BEGIN_DECLS

typedef struct _CamelImapStore CamelImapStore;
typedef struct _CamelImapStoreClass CamelImapStoreClass;
typedef struct _CamelImapStorePrivate CamelImapStorePrivate;

/* CamelFolderInfo flags */
#define CAMEL_IMAP_FOLDER_MARKED	     (1 << 16)
#define CAMEL_IMAP_FOLDER_UNMARKED	     (1 << 17)

typedef enum {
	IMAP_LEVEL_UNKNOWN,
	IMAP_LEVEL_IMAP4,
	IMAP_LEVEL_IMAP4REV1
} CamelImapServerLevel;

#define IMAP_CAPABILITY_IMAP4			(1 << 0)
#define IMAP_CAPABILITY_IMAP4REV1		(1 << 1)
#define IMAP_CAPABILITY_STATUS			(1 << 2)
#define IMAP_CAPABILITY_NAMESPACE		(1 << 3)
#define IMAP_CAPABILITY_UIDPLUS			(1 << 4)
#define IMAP_CAPABILITY_LITERALPLUS		(1 << 5)
#define IMAP_CAPABILITY_STARTTLS                (1 << 6)
#define IMAP_CAPABILITY_useful_lsub		(1 << 7)
#define IMAP_CAPABILITY_utf8_search		(1 << 8)
#define IMAP_CAPABILITY_XGWEXTENSIONS		(1 << 9)
#define IMAP_CAPABILITY_XGWMOVE			(1 << 10)
#define IMAP_CAPABILITY_LOGINDISABLED		(1 << 11)
#define IMAP_CAPABILITY_QUOTA			(1 << 12)

struct _CamelImapStore {
	CamelOfflineStore parent;
	CamelImapStorePrivate *priv;

	/* For processing IMAP commands and responses. */
	GStaticRecMutex command_and_response_lock;

	CamelStream *istream;
	CamelStream *ostream;

	struct _CamelImapStoreSummary *summary;

	/* Information about the command channel / connection status */
	guint connected : 1;
	guint preauthed : 1;

	/* broken server - don't use BODY, dont use partial fetches for message retrival */
	guint braindamaged : 1;
	guint renaming : 1;
	/* broken server - wont let us append with custom flags even if the folder allows them */
	guint nocustomappend : 1;

	gchar tag_prefix;
	guint32 command;
	CamelFolder *current_folder;

	/* Information about the server */
	CamelImapServerLevel server_level;
	guint32 capabilities;
	gchar dir_sep;
	GHashTable *authtypes;

	time_t refresh_stamp;

	GHashTable *known_alerts;
};

struct _CamelImapStoreClass {
	CamelOfflineStoreClass parent_class;
};

GType		camel_imap_store_get_type	(void);
gboolean	camel_imap_store_connected	(CamelImapStore *store,
						 GError **error);
gssize		camel_imap_store_readline	(CamelImapStore *store,
						 gchar **dest,
						 GCancellable *cancellable,
						 GError **error);

G_END_DECLS

#endif /* CAMEL_IMAP_STORE_H */
