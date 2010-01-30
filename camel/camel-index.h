/*
 *  Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 *  Authors: Michael Zucchi <notzed@ximian.com>
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

#ifndef _CAMEL_INDEX_H
#define _CAMEL_INDEX_H

#include <camel/camel-exception.h>
#include <camel/camel-object.h>

#define CAMEL_INDEX(obj)         CAMEL_CHECK_CAST (obj, camel_index_get_type (), CamelIndex)
#define CAMEL_INDEX_CLASS(klass) CAMEL_CHECK_CLASS_CAST (klass, camel_index_get_type (), CamelIndexClass)
#define CAMEL_IS_INDEX(obj)      CAMEL_CHECK_TYPE (obj, camel_index_get_type ())

G_BEGIN_DECLS

typedef struct _CamelIndex      CamelIndex;
typedef struct _CamelIndexClass CamelIndexClass;

#define CAMEL_INDEX_NAME(obj)         CAMEL_CHECK_CAST (obj, camel_index_name_get_type (), CamelIndexName)
#define CAMEL_INDEX_NAME_CLASS(klass) CAMEL_CHECK_CLASS_CAST (klass, camel_index_name_get_type (), CamelIndexNameClass)
#define CAMEL_IS_INDEX_NAME(obj)      CAMEL_CHECK_TYPE (obj, camel_index_name_get_type ())

typedef struct _CamelIndexName      CamelIndexName;
typedef struct _CamelIndexNameClass CamelIndexNameClass;

#define CAMEL_INDEX_CURSOR(obj)         CAMEL_CHECK_CAST (obj, camel_index_cursor_get_type (), CamelIndexCursor)
#define CAMEL_INDEX_CURSOR_CLASS(klass) CAMEL_CHECK_CLASS_CAST (klass, camel_index_cursor_get_type (), CamelIndexCursorClass)
#define CAMEL_IS_INDEX_CURSOR(obj)      CAMEL_CHECK_TYPE (obj, camel_index_cursor_get_type ())

typedef struct _CamelIndexCursor      CamelIndexCursor;
typedef struct _CamelIndexCursorClass CamelIndexCursorClass;

typedef gchar * (*CamelIndexNorm)(CamelIndex *idx, const gchar *word, gpointer data);

/* ********************************************************************** */

struct _CamelIndexCursor {
	CamelObject parent;

	struct _CamelIndexCursorPrivate *priv;

	CamelIndex *index;
};

struct _CamelIndexCursorClass {
	CamelObjectClass parent;

	const gchar * (*next) (CamelIndexCursor *idc);
	void         (*reset) (CamelIndexCursor *idc);
};

CamelType		   camel_index_cursor_get_type(void);

CamelIndexCursor  *camel_index_cursor_new(CamelIndex *idx, const gchar *name);

const gchar        *camel_index_cursor_next(CamelIndexCursor *idc);
void               camel_index_cursor_reset(CamelIndexCursor *idc);

/* ********************************************************************** */

struct _CamelIndexName {
	CamelObject parent;

	struct _CamelIndexNamePrivate *priv;

	CamelIndex *index;

	gchar *name;		/* name being indexed */

	GByteArray *buffer;	/* used for normalisation */
	GHashTable *words;	/* unique list of words */
};

struct _CamelIndexNameClass {
	CamelObjectClass parent;

	gint (*sync)(CamelIndexName *name);
	void (*add_word)(CamelIndexName *name, const gchar *word);
	gsize (*add_buffer)(CamelIndexName *name, const gchar *buffer, gsize len);
};

CamelType		   camel_index_name_get_type	(void);

CamelIndexName    *camel_index_name_new(CamelIndex *idx, const gchar *name);

void               camel_index_name_add_word(CamelIndexName *name, const gchar *word);
gsize             camel_index_name_add_buffer(CamelIndexName *name, const gchar *buffer, gsize len);

/* ********************************************************************** */

struct _CamelIndex {
	CamelObject parent;

	struct _CamelIndexPrivate *priv;

	gchar *path;
	guint32 version;
	guint32 flags;		/* open flags */
	guint32 state;

	CamelIndexNorm normalise;
	gpointer normalise_data;
};

struct _CamelIndexClass {
	CamelObjectClass parent_class;

	gint			(*sync)(CamelIndex *idx);
	gint			(*compress)(CamelIndex *idx);
	gint			(*delete)(CamelIndex *idx);

	gint			(*rename)(CamelIndex *idx, const gchar *path);

	gint			(*has_name)(CamelIndex *idx, const gchar *name);
	CamelIndexName *	(*add_name)(CamelIndex *idx, const gchar *name);
	gint			(*write_name)(CamelIndex *idx, CamelIndexName *idn);
	CamelIndexCursor *	(*find_name)(CamelIndex *idx, const gchar *name);
	void			(*delete_name)(CamelIndex *idx, const gchar *name);
	CamelIndexCursor *	(*find)(CamelIndex *idx, const gchar *word);

	CamelIndexCursor *      (*words)(CamelIndex *idx);
	CamelIndexCursor *      (*names)(CamelIndex *idx);
};

/* flags, stored in 'state', set with set_state */
#define CAMEL_INDEX_DELETED (1<<0)

CamelType		   camel_index_get_type	(void);

CamelIndex        *camel_index_new(const gchar *path, gint flags);
void               camel_index_construct(CamelIndex *, const gchar *path, gint flags);
gint		   camel_index_rename(CamelIndex *, const gchar *path);

void               camel_index_set_normalise(CamelIndex *idx, CamelIndexNorm func, gpointer data);

gint                camel_index_sync(CamelIndex *idx);
gint                camel_index_compress(CamelIndex *idx);
gint		   camel_index_delete(CamelIndex *idx);

gint                camel_index_has_name(CamelIndex *idx, const gchar *name);
CamelIndexName    *camel_index_add_name(CamelIndex *idx, const gchar *name);
gint                camel_index_write_name(CamelIndex *idx, CamelIndexName *idn);
CamelIndexCursor  *camel_index_find_name(CamelIndex *idx, const gchar *name);
void               camel_index_delete_name(CamelIndex *idx, const gchar *name);
CamelIndexCursor  *camel_index_find(CamelIndex *idx, const gchar *word);

CamelIndexCursor  *camel_index_words(CamelIndex *idx);
CamelIndexCursor  *camel_index_names(CamelIndex *idx);

G_END_DECLS

#endif /* _CAMEL_INDEX_H */
