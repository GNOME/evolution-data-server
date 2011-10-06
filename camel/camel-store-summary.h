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

#ifndef CAMEL_STORE_SUMMARY_H
#define CAMEL_STORE_SUMMARY_H

#include <stdio.h>

#include <camel/camel-enums.h>
#include <camel/camel-memchunk.h>
#include <camel/camel-mime-parser.h>
#include <camel/camel-object.h>

/* Standard GObject macros */
#define CAMEL_TYPE_STORE_SUMMARY \
	(camel_store_summary_get_type ())
#define CAMEL_STORE_SUMMARY(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_STORE_SUMMARY, CamelStoreSummary))
#define CAMEL_STORE_SUMMARY_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_STORE_SUMMARY, CamelStoreSummaryClass))
#define CAMEL_IS_STORE_SUMMARY(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_STORE_SUMMARY))
#define CAMEL_IS_STORE_SUMMARY_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_STORE_SUMMARY))
#define CAMEL_STORE_SUMMARY_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_STORE_SUMMARY, CamelStoreSummaryClass))

G_BEGIN_DECLS

typedef struct _CamelStoreSummary CamelStoreSummary;
typedef struct _CamelStoreSummaryClass CamelStoreSummaryClass;
typedef struct _CamelStoreSummaryPrivate CamelStoreSummaryPrivate;

typedef struct _CamelStoreInfo CamelStoreInfo;

#define CAMEL_STORE_INFO_FOLDER_UNKNOWN (~0)

enum {
	CAMEL_STORE_INFO_PATH = 0,
	CAMEL_STORE_INFO_NAME,
	CAMEL_STORE_INFO_LAST
};

struct _CamelStoreInfo {
	guint32 refcount;
	gchar *path;
	guint32 flags;
	guint32 unread;
	guint32 total;
};

typedef enum _CamelStoreSummaryFlags {
	CAMEL_STORE_SUMMARY_DIRTY = 1 << 0,
} CamelStoreSummaryFlags;

/**
 * CamelStoreSummaryLock:
 *
 * Since: 2.32
 **/
typedef enum {
	CAMEL_STORE_SUMMARY_SUMMARY_LOCK,
	CAMEL_STORE_SUMMARY_IO_LOCK,
	CAMEL_STORE_SUMMARY_REF_LOCK
} CamelStoreSummaryLock;

struct _CamelStoreSummary {
	CamelObject parent;
	CamelStoreSummaryPrivate *priv;

	/* header info */
	guint32 version;	/* version of base part of file */
	guint32 flags;		/* flags */
	guint32 count;		/* how many were saved/loaded */
	time_t time;		/* timestamp for this summary (for implementors to use) */

	/* sizes of memory objects */
	guint32 store_info_size;

	/* memory allocators (setup automatically) */
	CamelMemChunk *store_info_chunks;

	gchar *summary_path;

	GPtrArray *folders;	/* CamelStoreInfo's */
	GHashTable *folders_path; /* CamelStoreInfo's by path name */
};

struct _CamelStoreSummaryClass {
	CamelObjectClass parent_class;

	/* load/save the global info */
	gint (*summary_header_load)(CamelStoreSummary *, FILE *);
	gint (*summary_header_save)(CamelStoreSummary *, FILE *);

	/* create/save/load an individual message info */
	CamelStoreInfo * (*store_info_new)(CamelStoreSummary *, const gchar *path);
	CamelStoreInfo * (*store_info_load)(CamelStoreSummary *, FILE *);
	gint		  (*store_info_save)(CamelStoreSummary *, FILE *, CamelStoreInfo *);
	void		  (*store_info_free)(CamelStoreSummary *, CamelStoreInfo *);

	/* virtualise access methods */
	const gchar *(*store_info_string)(CamelStoreSummary *, const CamelStoreInfo *, gint);
	void (*store_info_set_string)(CamelStoreSummary *, CamelStoreInfo *, int, const gchar *);
};

GType			 camel_store_summary_get_type	(void);
CamelStoreSummary      *camel_store_summary_new	(void);

void camel_store_summary_set_filename (CamelStoreSummary *summary, const gchar *filename);

/* load/save the summary in its entirety */
gint camel_store_summary_load (CamelStoreSummary *summary);
gint camel_store_summary_save (CamelStoreSummary *summary);

/* only load the header */
gint camel_store_summary_header_load (CamelStoreSummary *summary);

/* set the dirty bit on the summary */
void camel_store_summary_touch (CamelStoreSummary *summary);

/* add a new raw summary item */
void camel_store_summary_add (CamelStoreSummary *summary, CamelStoreInfo *info);

/* build/add raw summary items */
CamelStoreInfo *camel_store_summary_add_from_path (CamelStoreSummary *summary, const gchar *path);

/* Just build raw summary items */
CamelStoreInfo *camel_store_summary_info_new (CamelStoreSummary *summary);
CamelStoreInfo *camel_store_summary_info_new_from_path (CamelStoreSummary *summary, const gchar *path);

void camel_store_summary_info_ref (CamelStoreSummary *summary, CamelStoreInfo *info);
void camel_store_summary_info_free (CamelStoreSummary *summary, CamelStoreInfo *info);

/* removes a summary item */
void camel_store_summary_remove (CamelStoreSummary *summary, CamelStoreInfo *info);
void camel_store_summary_remove_path (CamelStoreSummary *summary, const gchar *path);
void camel_store_summary_remove_index (CamelStoreSummary *summary, gint index);

/* remove all items */
void camel_store_summary_clear (CamelStoreSummary *summary);

/* lookup functions */
gint camel_store_summary_count (CamelStoreSummary *summary);
CamelStoreInfo *camel_store_summary_index (CamelStoreSummary *summary, gint index);
CamelStoreInfo *camel_store_summary_path (CamelStoreSummary *summary, const gchar *path);
GPtrArray *camel_store_summary_array (CamelStoreSummary *summary);
void camel_store_summary_array_free (CamelStoreSummary *summary, GPtrArray *array);

const gchar *camel_store_info_string (CamelStoreSummary *summary, const CamelStoreInfo *info, gint type);
void camel_store_info_set_string (CamelStoreSummary *summary, CamelStoreInfo *info, gint type, const gchar *value);

/* helper macro's */
#define camel_store_info_path(s, i) (camel_store_info_string((CamelStoreSummary *)s, (const CamelStoreInfo *)i, CAMEL_STORE_INFO_PATH))
#define camel_store_info_name(s, i) (camel_store_info_string((CamelStoreSummary *)s, (const CamelStoreInfo *)i, CAMEL_STORE_INFO_NAME))

void camel_store_summary_lock   (CamelStoreSummary *summary, CamelStoreSummaryLock lock);
void camel_store_summary_unlock (CamelStoreSummary *summary, CamelStoreSummaryLock lock);

struct _CamelFolderSummary;
gboolean camel_store_summary_connect_folder_summary (CamelStoreSummary *summary, const gchar *path, struct _CamelFolderSummary *folder_summary);
gboolean camel_store_summary_disconnect_folder_summary (CamelStoreSummary *summary, struct _CamelFolderSummary *folder_summary);

G_END_DECLS

#endif /* CAMEL_STORE_SUMMARY_H */
