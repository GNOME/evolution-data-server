/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 *  Authors: Jeffrey Stedfast <fejj@ximian.com>
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

#ifndef _CAMEL_MIME_FILTER_LINEWRAP_H
#define _CAMEL_MIME_FILTER_LINEWRAP_H

#include <camel/camel-mime-filter.h>

#define CAMEL_MIME_FILTER_LINEWRAP_TYPE         (camel_mime_filter_linewrap_get_type ())
#define CAMEL_MIME_FILTER_LINEWRAP(obj)         CAMEL_CHECK_CAST (obj, CAMEL_MIME_FILTER_LINEWRAP_TYPE, CamelMimeFilterLinewrap)
#define CAMEL_MIME_FILTER_LINEWRAP_CLASS(klass) CAMEL_CHECK_CLASS_CAST (klass, CAMEL_MIME_FILTER_LINEWRAP_TYPE, CamelMimeFilterLinewrapClass)
#define CAMEL_IS_MIME_FILTER_LINEWRAP(obj)      CAMEL_CHECK_TYPE (obj, CAMEL_MIME_FILTER_LINEWRAP_TYPE)

G_BEGIN_DECLS

enum {
	CAMEL_MIME_FILTER_LINEWRAP_NOINDENT = (1<<0), /* does not indent; it's forced for indent_char = 0 */
	CAMEL_MIME_FILTER_LINEWRAP_WORD     = (1<<1), /* indents on word boundary */
};

typedef struct _CamelMimeFilterLinewrapClass CamelMimeFilterLinewrapClass;

struct _CamelMimeFilterLinewrap {
	CamelMimeFilter parent;

	guint wrap_len;
	guint max_len;
	gchar indent;
	gint nchars;
	guint32 flags;
};

struct _CamelMimeFilterLinewrapClass {
	CamelMimeFilterClass parent_class;
};

CamelType camel_mime_filter_linewrap_get_type (void);

CamelMimeFilter *camel_mime_filter_linewrap_new (guint preferred_len, guint max_len, gchar indent_char, guint32 flags);

G_END_DECLS

#endif /* _CAMEL_MIME_FILTER_LINEWRAP_H */
