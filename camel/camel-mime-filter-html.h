/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
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

#ifndef _CAMEL_MIME_FILTER_HTML_H
#define _CAMEL_MIME_FILTER_HTML_H

#include <camel/camel-mime-filter.h>

#define CAMEL_MIME_FILTER_HTML(obj)         CAMEL_CHECK_CAST (obj, camel_mime_filter_html_get_type (), CamelMimeFilterHTML)
#define CAMEL_MIME_FILTER_HTML_CLASS(klass) CAMEL_CHECK_CLASS_CAST (klass, camel_mime_filter_html_get_type (), CamelMimeFilterHTMLClass)
#define CAMEL_IS_MIME_FILTER_HTML(obj)      CAMEL_CHECK_TYPE (obj, camel_mime_filter_html_get_type ())

G_BEGIN_DECLS

typedef struct _CamelMimeFilterHTMLClass CamelMimeFilterHTMLClass;
typedef struct _CamelMimeFilterHTML CamelMimeFilterHTML;

struct _CamelMimeFilterHTML {
	CamelMimeFilter parent;

	struct _CamelMimeFilterHTMLPrivate *priv;
};

struct _CamelMimeFilterHTMLClass {
	CamelMimeFilterClass parent_class;
};

CamelType		camel_mime_filter_html_get_type	(void);
CamelMimeFilterHTML      *camel_mime_filter_html_new	(void);

G_END_DECLS

#endif /* _CAMEL_MIME_FILTER_HTML_H */
