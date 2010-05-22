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

#ifndef _CAMEL_PARTITION_TABLE_H
#define _CAMEL_PARTITION_TABLE_H

#include <camel/camel-object.h>
#include <glib.h>

#include "camel-block-file.h"
#include "camel-list-utils.h"

#define CAMEL_TYPE_PARTITION_TABLE \
	(camel_partition_table_get_type ())
#define CAMEL_IS_PARTITION_TABLE(obj) \
	(CAMEL_CHECK_TYPE ((obj), CAMEL_TYPE_PARTITION_TABLE))

#define CAMEL_TYPE_KEY_TABLE \
	(camel_key_table_get_type ())
#define CAMEL_IS_KEY_TABLE(obj) \
	(CAMEL_CHECK_TYPE ((obj), CAMEL_TYPE_KEY_TABLE))

G_BEGIN_DECLS

/* ********************************************************************** */

/* CamelPartitionTable - index of key to keyid */

typedef guint32 camel_hash_t;	/* a hashed key */

typedef struct _CamelPartitionKey CamelPartitionKey;
typedef struct _CamelPartitionKeyBlock CamelPartitionKeyBlock;
typedef struct _CamelPartitionMap CamelPartitionMap;
typedef struct _CamelPartitionMapBlock CamelPartitionMapBlock;

typedef struct _CamelPartitionTable CamelPartitionTable;
typedef struct _CamelPartitionTableClass CamelPartitionTableClass;

struct _CamelPartitionKey {
	camel_hash_t hashid;
	camel_key_t keyid;
};

struct _CamelPartitionKeyBlock {
	guint32 used;
	struct _CamelPartitionKey keys[(CAMEL_BLOCK_SIZE-4)/sizeof(struct _CamelPartitionKey)];
};

struct _CamelPartitionMap {
	camel_hash_t hashid;
	camel_block_t blockid;
};

struct _CamelPartitionMapBlock {
	camel_block_t next;
	guint32 used;
	struct _CamelPartitionMap partition[(CAMEL_BLOCK_SIZE-8)/sizeof(struct _CamelPartitionMap)];
};

struct _CamelPartitionTable {
	CamelObject parent;

	struct _CamelPartitionTablePrivate *priv;

	CamelBlockFile *blocks;
	camel_block_t rootid;

	gint (*is_key)(CamelPartitionTable *cpi, const gchar *key, camel_key_t keyid, gpointer data);
	gpointer is_key_data;

	/* we keep a list of partition blocks active at all times */
	CamelDList partition;
};

struct _CamelPartitionTableClass {
	CamelObjectClass parent;
};

CamelType camel_partition_table_get_type(void);

CamelPartitionTable *camel_partition_table_new(struct _CamelBlockFile *bs, camel_block_t root);
gint camel_partition_table_sync(CamelPartitionTable *cpi);
gint camel_partition_table_add(CamelPartitionTable *cpi, const gchar *key, camel_key_t keyid);
camel_key_t camel_partition_table_lookup(CamelPartitionTable *cpi, const gchar *key);
void camel_partition_table_remove(CamelPartitionTable *cpi, const gchar *key);

/* ********************************************************************** */

/* CamelKeyTable - index of keyid to key and flag and data mapping */

typedef struct _CamelKeyBlock CamelKeyBlock;
typedef struct _CamelKeyRootBlock CamelKeyRootBlock;

typedef struct _CamelKeyTable CamelKeyTable;
typedef struct _CamelKeyTableClass CamelKeyTableClass;

struct _CamelKeyRootBlock {
	camel_block_t first;
	camel_block_t last;
	camel_key_t free;	/* free list */
};

struct _CamelKeyKey {
	camel_block_t data;
	guint offset:10;
	guint flags:22;
};

struct _CamelKeyBlock {
	camel_block_t next;
	guint32 used;
	union {
		struct _CamelKeyKey keys[(CAMEL_BLOCK_SIZE-8)/sizeof(struct _CamelKeyKey)];
		gchar keydata[CAMEL_BLOCK_SIZE-8];
	} u;
};

#define CAMEL_KEY_TABLE_MAX_KEY (128) /* max size of any key */

struct _CamelKeyTable {
	CamelObject parent;

	struct _CamelKeyTablePrivate *priv;

	CamelBlockFile *blocks;

	camel_block_t rootid;

	CamelKeyRootBlock *root;
	CamelBlock *root_block;
};

struct _CamelKeyTableClass {
	CamelObjectClass parent;
};

CamelType camel_key_table_get_type(void);

CamelKeyTable * camel_key_table_new(CamelBlockFile *bs, camel_block_t root);
gint camel_key_table_sync(CamelKeyTable *ki);
camel_key_t camel_key_table_add(CamelKeyTable *ki, const gchar *key, camel_block_t data, guint flags);
void camel_key_table_set_data(CamelKeyTable *ki, camel_key_t keyid, camel_block_t data);
void camel_key_table_set_flags(CamelKeyTable *ki, camel_key_t keyid, guint flags, guint set);
camel_block_t camel_key_table_lookup(CamelKeyTable *ki, camel_key_t keyid, gchar **key, guint *flags);
camel_key_t camel_key_table_next(CamelKeyTable *ki, camel_key_t next, gchar **keyp, guint *flagsp, camel_block_t *datap);

G_END_DECLS

#endif /* _CAMEL_PARTITION_TABLE_H */
