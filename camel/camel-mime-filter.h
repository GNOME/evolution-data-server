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

/* Abstract class for non-copying filters */

#ifndef _CAMEL_MIME_FILTER_H
#define _CAMEL_MIME_FILTER_H

#include <sys/types.h>
#include <camel/camel-object.h>

#define CAMEL_MIME_FILTER_TYPE         (camel_mime_filter_get_type ())
#define CAMEL_MIME_FILTER(obj)         CAMEL_CHECK_CAST (obj, camel_mime_filter_get_type (), CamelMimeFilter)
#define CAMEL_MIME_FILTER_CLASS(klass) CAMEL_CHECK_CLASS_CAST (klass, camel_mime_filter_get_type (), CamelMimeFilterClass)
#define CAMEL_IS_MIME_FILTER(obj)      CAMEL_CHECK_TYPE (obj, camel_mime_filter_get_type ())

G_BEGIN_DECLS

typedef struct _CamelMimeFilterClass CamelMimeFilterClass;

struct _CamelMimeFilter {
	CamelObject parent;

	struct _CamelMimeFilterPrivate *priv;

	gchar *outreal;		/* real malloc'd buffer */
	gchar *outbuf;		/* first 'writable' position allowed (outreal + outpre) */
	gchar *outptr;
	gsize outsize;
	gsize outpre;		/* prespace of this buffer */

	gchar *backbuf;
	gsize backsize;
	gsize backlen;		/* significant data there */
};

struct _CamelMimeFilterClass {
	CamelObjectClass parent_class;

	/* virtual functions */
	void (*filter)(CamelMimeFilter *f,
		       const gchar *in, gsize len, gsize prespace,
		       gchar **out, gsize *outlen, gsize *outprespace);
	void (*complete)(CamelMimeFilter *f,
			 const gchar *in, gsize len, gsize prespace,
			 gchar **out, gsize *outlen, gsize *outprespace);
	void (*reset)(CamelMimeFilter *f);
};

CamelType	      camel_mime_filter_get_type	(void);
CamelMimeFilter      *camel_mime_filter_new	(void);

void camel_mime_filter_filter(CamelMimeFilter *filter,
			      const gchar *in, gsize len, gsize prespace,
			      gchar **out, gsize *outlen, gsize *outprespace);

void camel_mime_filter_complete(CamelMimeFilter *filter,
				const gchar *in, gsize len, gsize prespace,
				gchar **out, gsize *outlen, gsize *outprespace);

void camel_mime_filter_reset(CamelMimeFilter *filter);

/* sets/returns number of bytes backed up on the input */
void camel_mime_filter_backup(CamelMimeFilter *filter, const gchar *data, gsize length);

/* ensure this much size available for filter output */
void camel_mime_filter_set_size(CamelMimeFilter *filter, gsize size, gint keep);

G_END_DECLS

#endif /* _CAMEL_MIME_FILTER_H */
