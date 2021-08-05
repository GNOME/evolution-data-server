/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This library is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 * Authors: Jeffrey Stedfast <fejj@ximian.com>
 *	    Michael Zucchi <notzed@ximian.com>
 */

#if !defined (__CAMEL_H_INSIDE__) && !defined (CAMEL_COMPILATION)
#error "Only <camel/camel.h> can be included directly."
#endif

#ifndef CAMEL_MIME_FILTER_CANON_H
#define CAMEL_MIME_FILTER_CANON_H

#include <camel/camel-mime-filter.h>

/* Standard GObject macros */
#define CAMEL_TYPE_MIME_FILTER_CANON \
	(camel_mime_filter_canon_get_type ())
#define CAMEL_MIME_FILTER_CANON(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_MIME_FILTER_CANON, CamelMimeFilterCanon))
#define CAMEL_MIME_FILTER_CANON_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_MIME_FILTER_CANON, CamelMimeFilterCanonClass))
#define CAMEL_IS_MIME_FILTER_CANON(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_MIME_FILTER_CANON))
#define CAMEL_IS_MIME_FILTER_CANON_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_MIME_FILTER_CANON))
#define CAMEL_MIME_FILTER_CANON_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_MIME_FILTER_CANON, CamelMimeFilterCanonClass))

G_BEGIN_DECLS

typedef struct _CamelMimeFilterCanon CamelMimeFilterCanon;
typedef struct _CamelMimeFilterCanonClass CamelMimeFilterCanonClass;
typedef struct _CamelMimeFilterCanonPrivate CamelMimeFilterCanonPrivate;

typedef enum {
	CAMEL_MIME_FILTER_CANON_CRLF = (1 << 0), /* canoncialise end of line to crlf, otherwise canonicalise to lf only */
	CAMEL_MIME_FILTER_CANON_FROM = (1 << 1), /* escape "^From " using quoted-printable semantics into "=46rom " */
	CAMEL_MIME_FILTER_CANON_STRIP = (1 << 2)	/* strip trailing space */
} CamelMimeFilterCanonFlags;

struct _CamelMimeFilterCanon {
	CamelMimeFilter parent;
	CamelMimeFilterCanonPrivate *priv;
};

struct _CamelMimeFilterCanonClass {
	CamelMimeFilterClass parent_class;

	/* Padding for future expansion */
	gpointer reserved[20];
};

GType		camel_mime_filter_canon_get_type (void);
CamelMimeFilter *
		camel_mime_filter_canon_new	(guint32 flags);

G_END_DECLS

#endif /* CAMEL_MIME_FILTER_CANON_H */
