/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 *  Authors: Dan Winship <danw@ximian.com>
 *           Jeffrey Stedfast <fejj@ximian.com>
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

#ifndef CAMEL_MIME_FILTER_CRLF_H
#define CAMEL_MIME_FILTER_CRLF_H

#include <camel/camel-mime-filter.h>

/* Standard GObject macros */
#define CAMEL_TYPE_MIME_FILTER_CRLF \
	(camel_mime_filter_crlf_get_type ())
#define CAMEL_MIME_FILTER_CRLF(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_MIME_FILTER_CRLF, CamelMimeFilterCRLF))
#define CAMEL_MIME_FILTER_CRLF_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_MIME_FILTER_CRLF, CamelMimeFilterCRLFClass))
#define CAMEL_IS_MIME_FILTER_CRLF(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_MIME_FILTER_CRLF))
#define CAMEL_IS_MIME_FILTER_CRLF_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_MIME_FILTER_CRLF))
#define CAMEL_MIME_FILTER_CRLF_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_MIME_FILTER_CRLF, CamelMimeFilterCRLFClass))

G_BEGIN_DECLS

typedef struct _CamelMimeFilterCRLF CamelMimeFilterCRLF;
typedef struct _CamelMimeFilterCRLFClass CamelMimeFilterCRLFClass;
typedef struct _CamelMimeFilterCRLFPrivate CamelMimeFilterCRLFPrivate;

typedef enum {
	CAMEL_MIME_FILTER_CRLF_ENCODE,
	CAMEL_MIME_FILTER_CRLF_DECODE
} CamelMimeFilterCRLFDirection;

typedef enum {
	CAMEL_MIME_FILTER_CRLF_MODE_CRLF_DOTS,
	CAMEL_MIME_FILTER_CRLF_MODE_CRLF_ONLY
} CamelMimeFilterCRLFMode;

struct _CamelMimeFilterCRLF {
	CamelMimeFilter parent;
	CamelMimeFilterCRLFPrivate *priv;
};

struct _CamelMimeFilterCRLFClass {
	CamelMimeFilterClass parent_class;
};

GType		camel_mime_filter_crlf_get_type	(void);
CamelMimeFilter *
		camel_mime_filter_crlf_new	(CamelMimeFilterCRLFDirection direction,
						 CamelMimeFilterCRLFMode mode);

G_END_DECLS

#endif /* CAMEL_MIME_FILTER_CRLF_H */
