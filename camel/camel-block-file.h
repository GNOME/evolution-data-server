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

#ifndef _CAMEL_BLOCK_FILE_H
#define _CAMEL_BLOCK_FILE_H

#include <camel/camel-object.h>
#include <camel/camel-list-utils.h>
#include <glib.h>
#include <stdio.h>
#include <sys/types.h>

#define CAMEL_TYPE_BLOCK_FILE \
	(camel_block_file_get_type ())
#define CAMEL_IS_BLOCK_FILE(obj) \
	(CAMEL_CHECK_TYPE ((obj), CAMEL_TYPE_BLOCK_FILE))

#define CAMEL_TYPE_KEY_FILE \
	(camel_key_file_get_type ())
#define CAMEL_IS_KEY_FILE(obj) \
	(CAMEL_CHECK_TYPE ((obj), CAMEL_TYPE_KEY_FILE))

G_BEGIN_DECLS

typedef guint32 camel_block_t;	/* block offset, absolute, bottom BLOCK_SIZE_BITS always 0 */
typedef guint32 camel_key_t;	/* this is a bitfield of (block offset:BLOCK_SIZE_BITS) */

typedef struct _CamelBlockRoot CamelBlockRoot;
typedef struct _CamelBlock CamelBlock;
typedef struct _CamelBlockFile CamelBlockFile;
typedef struct _CamelBlockFileClass CamelBlockFileClass;

#define CAMEL_BLOCK_FILE_SYNC (1<<0)

#define CAMEL_BLOCK_SIZE (1024)
#define CAMEL_BLOCK_SIZE_BITS (10) /* # bits to contain block_size bytes */

#define CAMEL_BLOCK_DIRTY (1<<0)
#define CAMEL_BLOCK_DETACHED (1<<1)

struct _CamelBlockRoot {
	gchar version[8];	/* version number */

	guint32 flags;		/* flags for file */
	guint32 block_size;	/* block size of this file */
	camel_block_t free;	/* free block list */
	camel_block_t last;	/* pointer to end of blocks */

	/* subclasses tack on, but no more than CAMEL_BLOCK_SIZE! */
};

/* LRU cache of blocks */
struct _CamelBlock {
	struct _CamelBlock *next;
	struct _CamelBlock *prev;

	camel_block_t id;
	guint32 flags;
	guint32 refcount;
	guint32 align00;

	guchar data[CAMEL_BLOCK_SIZE];
};

struct _CamelBlockFile {
	CamelObject parent;

	struct _CamelBlockFilePrivate *priv;

	gchar version[8];
	gchar *path;
	gint flags;

	gint fd;
	gsize block_size;

	CamelBlockRoot *root;
	CamelBlock *root_block;

	/* make private? */
	gint block_cache_limit;
	gint block_cache_count;
	CamelDList block_cache;
	GHashTable *blocks;
};

struct _CamelBlockFileClass {
	CamelObjectClass parent;

	gint (*validate_root)(CamelBlockFile *);
	gint (*init_root)(CamelBlockFile *);
};

CamelType camel_block_file_get_type(void);

CamelBlockFile *camel_block_file_new(const gchar *path, gint flags, const gchar version[8], gsize block_size);
gint camel_block_file_rename(CamelBlockFile *bs, const gchar *path);
gint camel_block_file_delete(CamelBlockFile *kf);

CamelBlock *camel_block_file_new_block(CamelBlockFile *bs);
gint camel_block_file_free_block(CamelBlockFile *bs, camel_block_t id);
CamelBlock *camel_block_file_get_block(CamelBlockFile *bs, camel_block_t id);
void camel_block_file_detach_block(CamelBlockFile *bs, CamelBlock *bl);
void camel_block_file_attach_block(CamelBlockFile *bs, CamelBlock *bl);
void camel_block_file_touch_block(CamelBlockFile *bs, CamelBlock *bl);
void camel_block_file_unref_block(CamelBlockFile *bs, CamelBlock *bl);
gint camel_block_file_sync_block(CamelBlockFile *bs, CamelBlock *bl);
gint camel_block_file_sync(CamelBlockFile *bs);

/* ********************************************************************** */

typedef struct _CamelKeyFile CamelKeyFile;
typedef struct _CamelKeyFileClass CamelKeyFileClass;

struct _CamelKeyFile {
	CamelObject parent;

	struct _CamelKeyFilePrivate *priv;

	FILE *fp;
	gchar *path;
	gint flags;
	off_t last;
};

struct _CamelKeyFileClass {
	CamelObjectClass parent;
};

CamelType      camel_key_file_get_type(void);

CamelKeyFile * camel_key_file_new(const gchar *path, gint flags, const gchar version[8]);
gint	       camel_key_file_rename(CamelKeyFile *kf, const gchar *path);
gint	       camel_key_file_delete(CamelKeyFile *kf);

gint            camel_key_file_write(CamelKeyFile *kf, camel_block_t *parent, gsize len, camel_key_t *records);
gint            camel_key_file_read(CamelKeyFile *kf, camel_block_t *start, gsize *len, camel_key_t **records);

G_END_DECLS

#endif /* _CAMEL_BLOCK_FILE_H */
