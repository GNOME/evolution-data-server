/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * Authors: Michael Zucchi <notzed@ximian.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#if !defined (__CAMEL_H_INSIDE__) && !defined (CAMEL_COMPILATION)
#error "Only <camel/camel.h> can be included directly."
#endif

#ifndef CAMEL_VEE_FOLDER_H
#define CAMEL_VEE_FOLDER_H

#include <camel/camel-folder.h>
#include <camel/camel-folder-search.h>
#include <camel/camel-store.h>
#include <camel/camel-vee-summary.h>

#define CAMEL_VEE_FOLDER(obj)         CAMEL_CHECK_CAST (obj, camel_vee_folder_get_type (), CamelVeeFolder)
#define CAMEL_VEE_FOLDER_CLASS(klass) CAMEL_CHECK_CLASS_CAST (klass, camel_vee_folder_get_type (), CamelVeeFolderClass)
#define CAMEL_IS_VEE_FOLDER(obj)      CAMEL_CHECK_TYPE (obj, camel_vee_folder_get_type ())
#define CAMEL_VEE_FOLDER_GET_CLASS(obj) \
	((CamelVeeFolderClass *) CAMEL_OBJECT_GET_CLASS (obj))

G_BEGIN_DECLS

typedef struct _CamelVeeFolder CamelVeeFolder;
typedef struct _CamelVeeFolderClass CamelVeeFolderClass;
typedef struct _CamelVeeFolderPrivate CamelVeeFolderPrivate;

typedef enum _CamelVeeFolderLock {
	CVF_SUMMARY_LOCK,
	CVF_SUBFOLDER_LOCK,
	CVF_CHANGED_LOCK
} CamelVeeFolderLock;

struct _CamelVeeFolder {
	CamelFolder parent;
	CamelVeeFolderPrivate *priv;

	gchar *expression;	/* query expression */

	guint32 flags;		/* folder open flags */

	CamelFolderChangeInfo *changes;
	CamelFolderSearch *search;

	/* only set-up if our parent is a vee-store, used also as a flag to
	 * say that this folder is part of the unmatched folder */
	struct _CamelVeeStore *parent_vee_store;
	GHashTable *hashes;
	GHashTable *loaded;
	gboolean deleted;
};

struct _CamelVeeFolderClass {
	CamelFolderClass parent_class;

	/* TODO: Some of this may need some additional work/thinking through, it works for now*/

	void (*add_folder)(CamelVeeFolder *, CamelFolder *);
	void (*remove_folder)(CamelVeeFolder *, CamelFolder *);
	gint (*rebuild_folder)(CamelVeeFolder *, CamelFolder *, CamelException *);

	void (*set_expression)(CamelVeeFolder *, const gchar *);

	/* Called for a folder-changed event on a source folder */
	void (*folder_changed)(CamelVeeFolder *, CamelFolder *sub, CamelFolderChangeInfo *changes);
	/* Called for a folder-renamed event on a source folder */
	void (*folder_renamed)(CamelVeeFolder *, CamelFolder *sub, const gchar *old);
};

#define CAMEL_UNMATCHED_NAME "UNMATCHED"

CamelType	      camel_vee_folder_get_type		(void);
CamelFolder  *camel_vee_folder_new		(CamelStore *parent_store, const gchar *full, guint32 flags);
void         camel_vee_folder_construct		(CamelVeeFolder *vf, CamelStore *parent_store, const gchar *full, const gchar *name, guint32 flags);

CamelFolder *camel_vee_folder_get_location(CamelVeeFolder *vf, const struct _CamelVeeMessageInfo *vinfo, gchar **realuid);

void         camel_vee_folder_add_folder        (CamelVeeFolder *vf, CamelFolder *sub);
void         camel_vee_folder_remove_folder     (CamelVeeFolder *vf, CamelFolder *sub);
void	     camel_vee_folder_set_folders	(CamelVeeFolder *vf, GList *folders);
gint          camel_vee_folder_rebuild_folder(CamelVeeFolder *vf, CamelFolder *sub, CamelException *ex);
void	     camel_vee_folder_set_expression	(CamelVeeFolder *vf, const gchar *expr);

void	     camel_vee_folder_mask_event_folder_changed (CamelVeeFolder *vf, CamelFolder *sub);
void	     camel_vee_folder_unmask_event_folder_changed (CamelVeeFolder *vf, CamelFolder *sub);

void	     camel_vee_folder_hash_folder	(CamelFolder *folder, gchar buffer[8]);
void	     camel_vee_folder_sync_headers (CamelFolder *vf, CamelException *ex);

gint camel_vee_folder_get_unread_vfolder (CamelVeeFolder *folder);
void camel_vee_folder_set_unread_vfolder (CamelVeeFolder *folder, gint unread_vfolder);

void         camel_vee_folder_lock		(CamelVeeFolder *folder, CamelVeeFolderLock lock);
void         camel_vee_folder_unlock		(CamelVeeFolder *folder, CamelVeeFolderLock lock);

G_END_DECLS

#endif /* CAMEL_VEE_FOLDER_H */
