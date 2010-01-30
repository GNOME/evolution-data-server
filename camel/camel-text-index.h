
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

#ifndef _CAMEL_TEXT_INDEX_H
#define _CAMEL_TEXT_INDEX_H

#include <camel/camel-exception.h>
#include <camel/camel-object.h>
#include "camel-index.h"

#define CAMEL_TEXT_INDEX(obj)         CAMEL_CHECK_CAST (obj, camel_text_index_get_type (), CamelTextIndex)
#define CAMEL_TEXT_INDEX_CLASS(klass) CAMEL_CHECK_CLASS_CAST (klass, camel_text_index_get_type (), CamelTextIndexClass)
#define CAMEL_IS_TEXT_INDEX(obj)      CAMEL_CHECK_TYPE (obj, camel_text_index_get_type ())

G_BEGIN_DECLS

typedef struct _CamelTextIndex      CamelTextIndex;
typedef struct _CamelTextIndexClass CamelTextIndexClass;

typedef struct _CamelTextIndexName      CamelTextIndexName;
typedef struct _CamelTextIndexNameClass CamelTextIndexNameClass;

typedef struct _CamelTextIndexCursor      CamelTextIndexCursor;
typedef struct _CamelTextIndexCursorClass CamelTextIndexCursorClass;

typedef struct _CamelTextIndexKeyCursor      CamelTextIndexKeyCursor;
typedef struct _CamelTextIndexKeyCursorClass CamelTextIndexKeyCursorClass;

typedef void (*CamelTextIndexFunc)(CamelTextIndex *idx, const gchar *word, gchar *buffer);

/* ********************************************************************** */

struct _CamelTextIndexCursor {
	CamelIndexCursor parent;

	struct _CamelTextIndexCursorPrivate *priv;
};

struct _CamelTextIndexCursorClass {
	CamelIndexCursorClass parent;
};

CamelType camel_text_index_cursor_get_type(void);

/* ********************************************************************** */

struct _CamelTextIndexKeyCursor {
	CamelIndexCursor parent;

	struct _CamelTextIndexKeyCursorPrivate *priv;
};

struct _CamelTextIndexKeyCursorClass {
	CamelIndexCursorClass parent;
};

CamelType camel_text_index_key_cursor_get_type(void);

/* ********************************************************************** */

struct _CamelTextIndexName {
	CamelIndexName parent;

	struct _CamelTextIndexNamePrivate *priv;
};

struct _CamelTextIndexNameClass {
	CamelIndexNameClass parent;
};

CamelType camel_text_index_name_get_type(void);

/* ********************************************************************** */

struct _CamelTextIndex {
	CamelIndex parent;

	struct _CamelTextIndexPrivate *priv;
};

struct _CamelTextIndexClass {
	CamelIndexClass parent_class;
};

CamelType		   camel_text_index_get_type	(void);
CamelTextIndex    *camel_text_index_new(const gchar *path, gint flags);

/* static utility functions */
gint camel_text_index_check(const gchar *path);
gint camel_text_index_rename(const gchar *old, const gchar *new);
gint camel_text_index_remove(const gchar *old);

void camel_text_index_dump(CamelTextIndex *idx);
void camel_text_index_info(CamelTextIndex *idx);
void camel_text_index_validate(CamelTextIndex *idx);

G_END_DECLS

#endif /* _CAMEL_TEXT_INDEX_H */
