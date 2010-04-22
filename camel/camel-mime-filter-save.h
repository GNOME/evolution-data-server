/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Authors: Jeffrey Stedfast <fejj@ximian.com>
 *
 *  Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */

#if !defined (__CAMEL_H_INSIDE__) && !defined (CAMEL_COMPILATION)
#error "Only <camel/camel.h> can be included directly."
#endif

#ifndef CAMEL_MIME_FILTER_SAVE_H
#define CAMEL_MIME_FILTER_SAVE_H

#include <camel/camel-mime-filter.h>
#include <camel/camel-seekable-stream.h>

/* Standard GObject macros */
#define CAMEL_TYPE_MIME_FILTER_SAVE \
	(camel_mime_filter_save_get_type ())
#define CAMEL_MIME_FILTER_SAVE(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_MIME_FILTER_SAVE, CamelMimeFilterSave))
#define CAMEL_MIME_FILTER_SAVE_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_MIME_FILTER_SAVE, CamelMimeFilterSaveClass))
#define CAMEL_IS_MIME_FILTER_SAVE(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_MIME_FILTER_SAVE))
#define CAMEL_IS_MIME_FILTER_SAVE_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_MIME_FILTER_SAVE))
#define CAMEL_MIME_FILTER_SAVE_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_MIME_FILTER_SAVE, CamelMimeFilterSaveClass))

G_BEGIN_DECLS

typedef struct _CamelMimeFilterSave CamelMimeFilterSave;
typedef struct _CamelMimeFilterSaveClass CamelMimeFilterSaveClass;
typedef struct _CamelMimeFilterSavePrivate CamelMimeFilterSavePrivate;

struct _CamelMimeFilterSave {
	CamelMimeFilter parent;
	CamelMimeFilterSavePrivate *priv;
};

struct _CamelMimeFilterSaveClass {
	CamelMimeFilterClass parent_class;
};

GType		camel_mime_filter_save_get_type	(void);
CamelMimeFilter *
		camel_mime_filter_save_new	(CamelStream *stream);

G_END_DECLS

#endif /* CAMEL_MIME_FILTER_SAVE_H */
