/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * camel-disco-diary.h: class for logging disconnected operation
 *
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

#if !defined (__CAMEL_H_INSIDE__) && !defined (CAMEL_COMPILATION)
#error "Only <camel/camel.h> can be included directly."
#endif

#ifndef CAMEL_DISABLE_DEPRECATED

#ifndef CAMEL_DISCO_DIARY_H
#define CAMEL_DISCO_DIARY_H

#include <stdarg.h>
#include <stdio.h>

#include <camel/camel-disco-store.h>

/* Standard GObject macros */
#define CAMEL_TYPE_DISCO_DIARY \
	(camel_disco_diary_get_type ())
#define CAMEL_DISCO_DIARY(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_DISCO_DIARY, CamelDiscoDiary))
#define CAMEL_DISCO_DIARY_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_DISCO_DIARY, CamelDiscoDiaryClass))
#define CAMEL_IS_DISCO_DIARY(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_DISCO_DIARY))
#define CAMEL_IS_DISCO_DIARY_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_DISCO_DIARY))
#define CAMEL_DISCO_DIARY_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_DISCO_DIARY, CamelDiscoDiaryClass))

G_BEGIN_DECLS

typedef struct _CamelDiscoDiary CamelDiscoDiary;
typedef struct _CamelDiscoDiaryClass CamelDiscoDiaryClass;

typedef enum {
	CAMEL_DISCO_DIARY_END = 0,

	CAMEL_DISCO_DIARY_FOLDER_EXPUNGE,
	CAMEL_DISCO_DIARY_FOLDER_APPEND,
	CAMEL_DISCO_DIARY_FOLDER_TRANSFER
} CamelDiscoDiaryAction;

struct _CamelDiscoDiary {
	CamelObject parent;

	CamelDiscoStore *store;
	FILE *file;
	GHashTable *folders, *uidmap;
};

struct _CamelDiscoDiaryClass {
	CamelObjectClass parent_class;
};

GType		camel_disco_diary_get_type	(void);
CamelDiscoDiary *
		camel_disco_diary_new		(CamelDiscoStore *store,
						 const gchar *filename,
						 GError **error);
gboolean	camel_disco_diary_empty		(CamelDiscoDiary *diary);
void		camel_disco_diary_log		(CamelDiscoDiary *diary,
						 CamelDiscoDiaryAction action,
						 ...);
void		camel_disco_diary_replay	(CamelDiscoDiary *diary,
						 GCancellable *cancellable,
						 GError **error);

/* Temporary->Permanent UID map stuff */
void		camel_disco_diary_uidmap_add	(CamelDiscoDiary *diary,
						 const gchar *old_uid,
						 const gchar *new_uid);
const gchar *	camel_disco_diary_uidmap_lookup	(CamelDiscoDiary *diary,
						 const gchar *uid);

G_END_DECLS

#endif /* CAMEL_DISCO_DIARY_H */

#endif /* CAMEL_DISABLE_DEPRECATED */
