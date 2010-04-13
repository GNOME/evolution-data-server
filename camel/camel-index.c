/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8; fill-column: 160 -*- */
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include "camel-index.h"
#include "camel-object.h"

#define w(x)
#define io(x)
#define d(x) /*(printf ("%s (%d): ", __FILE__, __LINE__),(x))*/

#define CAMEL_INDEX_VERSION (0x01)

struct _CamelIndexPrivate {
	gpointer dummy;
};

#define _PRIVATE(o) (((CamelIndex *)(o))->priv)

/* ********************************************************************** */
/* CamelIndex */
/* ********************************************************************** */

static CamelObjectClass *camel_index_parent;

static void
index_finalize (CamelIndex *index)
{
	g_free (index->path);
	g_free (index->priv);
}

static void
camel_index_class_init (CamelIndexClass *klass)
{
	camel_index_parent = CAMEL_OBJECT_CLASS(camel_type_get_global_classfuncs(camel_object_get_type()));
}

static void
camel_index_init (CamelIndex *idx)
{
	struct _CamelIndexPrivate *p;

	p = _PRIVATE(idx) = g_malloc0(sizeof(*p));

	idx->version = CAMEL_INDEX_VERSION;
}

CamelType
camel_index_get_type(void)
{
	static CamelType type = CAMEL_INVALID_TYPE;

	if (type == CAMEL_INVALID_TYPE) {
		type = camel_type_register(camel_object_get_type(), "CamelIndex",
					   sizeof (CamelIndex),
					   sizeof (CamelIndexClass),
					   (CamelObjectClassInitFunc) camel_index_class_init,
					   NULL,
					   (CamelObjectInitFunc) camel_index_init,
					   (CamelObjectFinalizeFunc) index_finalize);
	}

	return type;
}

CamelIndex *
camel_index_new(const gchar *path, gint flags)
{
	CamelIndex *idx;

	idx = (CamelIndex *)camel_object_new(camel_index_get_type());
	camel_index_construct (idx, path, flags);

	return idx;
}

void
camel_index_construct (CamelIndex *idx, const gchar *path, gint flags)
{
	g_free (idx->path);
	idx->path = g_strdup_printf ("%s.index", path);
	idx->flags = flags;
}

gint
camel_index_rename (CamelIndex *idx, const gchar *path)
{
	CamelIndexClass *class;

	g_return_val_if_fail (CAMEL_IS_INDEX (idx), -1);

	class = CAMEL_INDEX_GET_CLASS (idx);
	g_return_val_if_fail (class->rename != NULL, -1);

	if ((idx->state & CAMEL_INDEX_DELETED) == 0)
		return class->rename (idx, path);
	else {
		errno = ENOENT;
		return -1;
	}
}

void
camel_index_set_normalize (CamelIndex *idx, CamelIndexNorm func, gpointer data)
{
	g_return_if_fail (CAMEL_IS_INDEX (idx));

	idx->normalize = func;
	idx->normalize_data = data;
}

gint
camel_index_sync (CamelIndex *idx)
{
	CamelIndexClass *class;

	g_return_val_if_fail (CAMEL_IS_INDEX (idx), -1);

	class = CAMEL_INDEX_GET_CLASS (idx);
	g_return_val_if_fail (class->sync != NULL, -1);

	if ((idx->state & CAMEL_INDEX_DELETED) == 0)
		return class->sync (idx);
	else {
		errno = ENOENT;
		return -1;
	}
}

gint
camel_index_compress (CamelIndex *idx)
{
	CamelIndexClass *class;

	g_return_val_if_fail (CAMEL_IS_INDEX (idx), -1);

	class = CAMEL_INDEX_GET_CLASS (idx);
	g_return_val_if_fail (class->compress != NULL, -1);

	if ((idx->state & CAMEL_INDEX_DELETED) == 0)
		return class->compress (idx);
	else {
		errno = ENOENT;
		return -1;
	}
}

gint
camel_index_delete (CamelIndex *idx)
{
	CamelIndexClass *class;
	gint ret;

	g_return_val_if_fail (CAMEL_IS_INDEX (idx), -1);

	class = CAMEL_INDEX_GET_CLASS (idx);
	g_return_val_if_fail (class->delete != NULL, -1);

	if ((idx->state & CAMEL_INDEX_DELETED) == 0) {
		ret = class->delete (idx);
		idx->state |= CAMEL_INDEX_DELETED;
	} else {
		errno = ENOENT;
		ret = -1;
	}

	return ret;
}

gint
camel_index_has_name (CamelIndex *idx, const gchar *name)
{
	CamelIndexClass *class;

	g_return_val_if_fail (CAMEL_IS_INDEX (idx), FALSE);

	class = CAMEL_INDEX_GET_CLASS (idx);
	g_return_val_if_fail (class->has_name != NULL, FALSE);

	if ((idx->state & CAMEL_INDEX_DELETED) == 0)
		return class->has_name (idx, name);
	else
		return FALSE;
}

CamelIndexName *
camel_index_add_name (CamelIndex *idx, const gchar *name)
{
	CamelIndexClass *class;

	g_return_val_if_fail (CAMEL_IS_INDEX (idx), NULL);

	class = CAMEL_INDEX_GET_CLASS (idx);
	g_return_val_if_fail (class->add_name != NULL, NULL);

	if ((idx->state & CAMEL_INDEX_DELETED) == 0)
		return class->add_name (idx, name);
	else
		return NULL;
}

gint
camel_index_write_name (CamelIndex *idx, CamelIndexName *idn)
{
	CamelIndexClass *class;

	g_return_val_if_fail (CAMEL_IS_INDEX (idx), -1);

	class = CAMEL_INDEX_GET_CLASS (idx);
	g_return_val_if_fail (class->write_name != NULL, -1);

	if ((idx->state & CAMEL_INDEX_DELETED) == 0)
		return class->write_name (idx, idn);
	else {
		errno = ENOENT;
		return -1;
	}
}

CamelIndexCursor *
camel_index_find_name (CamelIndex *idx, const gchar *name)
{
	CamelIndexClass *class;

	g_return_val_if_fail (CAMEL_IS_INDEX (idx), NULL);

	class = CAMEL_INDEX_GET_CLASS (idx);
	g_return_val_if_fail (class->find_name != NULL, NULL);

	if ((idx->state & CAMEL_INDEX_DELETED) == 0)
		return class->find_name (idx, name);
	else
		return NULL;
}

void
camel_index_delete_name (CamelIndex *idx, const gchar *name)
{
	CamelIndexClass *class;

	g_return_if_fail (CAMEL_IS_INDEX (idx));

	class = CAMEL_INDEX_GET_CLASS (idx);
	g_return_if_fail (class->delete_name != NULL);

	if ((idx->state & CAMEL_INDEX_DELETED) == 0)
		class->delete_name (idx, name);
}

CamelIndexCursor *
camel_index_find (CamelIndex *idx, const gchar *word)
{
	CamelIndexClass *class;
	CamelIndexCursor *ret;
	gchar *b = (gchar *) word;

	g_return_val_if_fail (CAMEL_IS_INDEX (idx), NULL);

	class = CAMEL_INDEX_GET_CLASS (idx);
	g_return_val_if_fail (class->find != NULL, NULL);

	if ((idx->state & CAMEL_INDEX_DELETED) != 0)
		return NULL;

	if (idx->normalize)
		b = idx->normalize (idx, word, idx->normalize_data);

	ret = class->find (idx, b);

	if (b != word)
		g_free (b);

	return ret;
}

CamelIndexCursor *
camel_index_words (CamelIndex *idx)
{
	CamelIndexClass *class;

	g_return_val_if_fail (CAMEL_IS_INDEX (idx), NULL);

	class = CAMEL_INDEX_GET_CLASS (idx);
	g_return_val_if_fail (class->words != NULL, NULL);

	if ((idx->state & CAMEL_INDEX_DELETED) == 0)
		return class->words (idx);
	else
		return NULL;
}

CamelIndexCursor *
camel_index_names (CamelIndex *idx)
{
	CamelIndexClass *class;

	g_return_val_if_fail (CAMEL_IS_INDEX (idx), NULL);

	class = CAMEL_INDEX_GET_CLASS (idx);
	g_return_val_if_fail (class->names != NULL, NULL);

	if ((idx->state & CAMEL_INDEX_DELETED) == 0)
		return class->names (idx);
	else
		return NULL;
}

/* ********************************************************************** */
/* CamelIndexName */
/* ********************************************************************** */

static CamelObjectClass *camel_index_name_parent;

static void
index_name_finalize(CamelIndexName *idn)
{
	if (idn->index)
		camel_object_unref (idn->index);
}

static void
camel_index_name_class_init(CamelIndexNameClass *klass)
{
	camel_index_name_parent = CAMEL_OBJECT_CLASS(camel_type_get_global_classfuncs(camel_object_get_type()));
}

static void
camel_index_name_init(CamelIndexName *idn)
{
}

CamelType
camel_index_name_get_type(void)
{
	static CamelType type = CAMEL_INVALID_TYPE;

	if (type == CAMEL_INVALID_TYPE) {
		type = camel_type_register(camel_object_get_type(), "CamelIndexName",
					   sizeof (CamelIndexName),
					   sizeof (CamelIndexNameClass),
					   (CamelObjectClassInitFunc) camel_index_name_class_init,
					   NULL,
					   (CamelObjectInitFunc) camel_index_name_init,
					   (CamelObjectFinalizeFunc) index_name_finalize);
	}

	return type;
}

CamelIndexName *
camel_index_name_new (CamelIndex *idx, const gchar *name)
{
	CamelIndexName *idn;

	idn = (CamelIndexName *)camel_object_new(camel_index_name_get_type());
	idn->index = camel_object_ref (idx);

	return idn;
}

void
camel_index_name_add_word (CamelIndexName *idn,
                           const gchar *word)
{
	CamelIndexNameClass *class;
	gchar *b = (gchar *)word;

	g_return_if_fail (CAMEL_IS_INDEX_NAME (idn));

	class = CAMEL_INDEX_NAME_GET_CLASS (idn);
	g_return_if_fail (class->add_word != NULL);

	if (idn->index->normalize)
		b = idn->index->normalize (idn->index, word, idn->index->normalize_data);

	class->add_word (idn, b);

	if (b != word)
		g_free (b);
}

gsize
camel_index_name_add_buffer (CamelIndexName *idn,
                             const gchar *buffer,
                             gsize len)
{
	CamelIndexNameClass *class;

	g_return_val_if_fail (CAMEL_IS_INDEX_NAME (idn), 0);

	class = CAMEL_INDEX_NAME_GET_CLASS (idn);
	g_return_val_if_fail (class->add_buffer != NULL, 0);

	return class->add_buffer (idn, buffer, len);
}

/* ********************************************************************** */
/* CamelIndexCursor */
/* ********************************************************************** */

static CamelObjectClass *camel_index_cursor_parent;

static void
index_cursor_finalize (CamelIndexCursor *idc)
{
	if (idc->index)
		camel_object_unref (idc->index);
}

static void
camel_index_cursor_class_init (CamelIndexCursorClass *klass)
{
	camel_index_cursor_parent = CAMEL_OBJECT_CLASS(camel_type_get_global_classfuncs(camel_object_get_type()));
}

static void
camel_index_cursor_init (CamelIndexCursor *idc)
{
}

CamelType
camel_index_cursor_get_type(void)
{
	static CamelType type = CAMEL_INVALID_TYPE;

	if (type == CAMEL_INVALID_TYPE) {
		type = camel_type_register(camel_object_get_type(), "CamelIndexCursor",
					   sizeof (CamelIndexCursor),
					   sizeof (CamelIndexCursorClass),
					   (CamelObjectClassInitFunc) camel_index_cursor_class_init,
					   NULL,
					   (CamelObjectInitFunc) camel_index_cursor_init,
					   (CamelObjectFinalizeFunc) index_cursor_finalize);
	}

	return type;
}

CamelIndexCursor *
camel_index_cursor_new (CamelIndex *idx, const gchar *name)
{
	CamelIndexCursor *idc;

	idc = (CamelIndexCursor *)camel_object_new(camel_index_cursor_get_type());
	idc->index = camel_object_ref (idx);

	return idc;
}

const gchar *
camel_index_cursor_next (CamelIndexCursor *idc)
{
	CamelIndexCursorClass *class;

	g_return_val_if_fail (CAMEL_IS_INDEX_CURSOR (idc), NULL);

	class = CAMEL_INDEX_CURSOR_GET_CLASS (idc);
	g_return_val_if_fail (class->next != NULL, NULL);

	return class->next (idc);
}

void
camel_index_cursor_reset (CamelIndexCursor *idc)
{
	CamelIndexCursorClass *class;

	g_return_if_fail (CAMEL_IS_INDEX_CURSOR (idc));

	class = CAMEL_INDEX_CURSOR_GET_CLASS (idc);
	g_return_if_fail (class->reset != NULL);

	class->reset (idc);
}
