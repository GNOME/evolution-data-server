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

#ifndef _CAMEL_STORE_SUMMARY_H
#define _CAMEL_STORE_SUMMARY_H

#include <stdio.h>

#include <glib.h>

#include <camel/camel-mime-parser.h>
#include <camel/camel-object.h>
#include <camel/camel-url.h>

#define CAMEL_STORE_SUMMARY(obj)         CAMEL_CHECK_CAST (obj, camel_store_summary_get_type (), CamelStoreSummary)
#define CAMEL_STORE_SUMMARY_CLASS(klass) CAMEL_CHECK_CLASS_CAST (klass, camel_store_summary_get_type (), CamelStoreSummaryClass)
#define CAMEL_IS_STORE_SUMMARY(obj)      CAMEL_CHECK_TYPE (obj, camel_store_summary_get_type ())

G_BEGIN_DECLS

typedef struct _CamelStoreSummary      CamelStoreSummary;
typedef struct _CamelStoreSummaryClass CamelStoreSummaryClass;

typedef struct _CamelStoreInfo CamelStoreInfo;

/* FIXME: this needs to track the CAMEL_FOLDER_* flags in camel-store.h */
typedef enum _CamelStoreInfoFlags {
	CAMEL_STORE_INFO_FOLDER_NOSELECT = 1<<0,
	CAMEL_STORE_INFO_FOLDER_NOINFERIORS = 1<<1,
	CAMEL_STORE_INFO_FOLDER_CHILDREN = 1<<2,
	CAMEL_STORE_INFO_FOLDER_NOCHILDREN = 1<<3,
	CAMEL_STORE_INFO_FOLDER_SUBSCRIBED = 1<<4,
	CAMEL_STORE_INFO_FOLDER_VIRTUAL = 1<<5,
	CAMEL_STORE_INFO_FOLDER_SYSTEM = 1<<6,
	CAMEL_STORE_INFO_FOLDER_VTRASH = 1<<7,
	CAMEL_STORE_INFO_FOLDER_SHARED_BY_ME = 1<<8,
	CAMEL_STORE_INFO_FOLDER_SHARED_TO_ME = 1<<9,

	/* not in camle-store.h yet */
	CAMEL_STORE_INFO_FOLDER_READONLY = 1<<13,
	CAMEL_STORE_INFO_FOLDER_CHECK_FOR_NEW = 1<<14,

	CAMEL_STORE_INFO_FOLDER_FLAGGED = 1<<31
} CamelStoreInfoFlags;

#define CAMEL_STORE_INFO_FOLDER_UNKNOWN (~0)

enum {
	CAMEL_STORE_INFO_PATH = 0,
	CAMEL_STORE_INFO_NAME,
	CAMEL_STORE_INFO_URI,
	CAMEL_STORE_INFO_LAST
};

struct _CamelStoreInfo {
	guint32 refcount;
	gchar *uri;
	gchar *path;
	guint32 flags;
	guint32 unread;
	guint32 total;
};

typedef enum _CamelStoreSummaryFlags {
	CAMEL_STORE_SUMMARY_DIRTY = 1<<0,
	CAMEL_STORE_SUMMARY_FRAGMENT = 1<<1 /* path name is stored in fragment rather than path */
} CamelStoreSummaryFlags;

struct _CamelStoreSummary {
	CamelObject parent;

	struct _CamelStoreSummaryPrivate *priv;

	/* header info */
	guint32 version;	/* version of base part of file */
	guint32 flags;		/* flags */
	guint32 count;		/* how many were saved/loaded */
	time_t time;		/* timestamp for this summary (for implementors to use) */
	struct _CamelURL *uri_base;	/* url of base part of summary */

	/* sizes of memory objects */
	guint32 store_info_size;

	/* memory allocators (setup automatically) */
	struct _EMemChunk *store_info_chunks;

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

CamelType			 camel_store_summary_get_type	(void);
CamelStoreSummary      *camel_store_summary_new	(void);

void camel_store_summary_set_filename(CamelStoreSummary *summary, const gchar *filename);
void camel_store_summary_set_uri_base(CamelStoreSummary *summary, CamelURL *base);

/* load/save the summary in its entirety */
gint camel_store_summary_load(CamelStoreSummary *summary);
gint camel_store_summary_save(CamelStoreSummary *summary);

/* only load the header */
gint camel_store_summary_header_load(CamelStoreSummary *summary);

/* set the dirty bit on the summary */
void camel_store_summary_touch(CamelStoreSummary *summary);

/* add a new raw summary item */
void camel_store_summary_add(CamelStoreSummary *summary, CamelStoreInfo *info);

/* build/add raw summary items */
CamelStoreInfo *camel_store_summary_add_from_path(CamelStoreSummary *summary, const gchar *path);

/* Just build raw summary items */
CamelStoreInfo *camel_store_summary_info_new(CamelStoreSummary *summary);
CamelStoreInfo *camel_store_summary_info_new_from_path(CamelStoreSummary *summary, const gchar *path);

void camel_store_summary_info_ref(CamelStoreSummary *summary, CamelStoreInfo *info);
void camel_store_summary_info_free(CamelStoreSummary *summary, CamelStoreInfo *info);

/* removes a summary item */
void camel_store_summary_remove(CamelStoreSummary *summary, CamelStoreInfo *info);
void camel_store_summary_remove_path(CamelStoreSummary *summary, const gchar *path);
void camel_store_summary_remove_index(CamelStoreSummary *summary, gint index);

/* remove all items */
void camel_store_summary_clear(CamelStoreSummary *summary);

/* lookup functions */
gint camel_store_summary_count(CamelStoreSummary *summary);
CamelStoreInfo *camel_store_summary_index(CamelStoreSummary *summary, gint index);
CamelStoreInfo *camel_store_summary_path(CamelStoreSummary *summary, const gchar *path);
GPtrArray *camel_store_summary_array(CamelStoreSummary *summary);
void camel_store_summary_array_free(CamelStoreSummary *summary, GPtrArray *array);

const gchar *camel_store_info_string(CamelStoreSummary *summary, const CamelStoreInfo *info, gint type);
void camel_store_info_set_string(CamelStoreSummary *summary, CamelStoreInfo *info, gint type, const gchar *value);

/* helper macro's */
#define camel_store_info_path(s, i) (camel_store_info_string((CamelStoreSummary *)s, (const CamelStoreInfo *)i, CAMEL_STORE_INFO_PATH))
#define camel_store_info_uri(s, i) (camel_store_info_string((CamelStoreSummary *)s, (const CamelStoreInfo *)i, CAMEL_STORE_INFO_URI))
#define camel_store_info_name(s, i) (camel_store_info_string((CamelStoreSummary *)s, (const CamelStoreInfo *)i, CAMEL_STORE_INFO_NAME))

G_END_DECLS

#endif /* _CAMEL_STORE_SUMMARY_H */
