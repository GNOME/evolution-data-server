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
	size_t outsize;
	size_t outpre;		/* prespace of this buffer */

	gchar *backbuf;
	size_t backsize;
	size_t backlen;		/* significant data there */
};

struct _CamelMimeFilterClass {
	CamelObjectClass parent_class;

	/* virtual functions */
	void (*filter)(CamelMimeFilter *f,
		       gchar *in, size_t len, size_t prespace,
		       gchar **out, size_t *outlen, size_t *outprespace);
	void (*complete)(CamelMimeFilter *f,
			 gchar *in, size_t len, size_t prespace,
			 gchar **out, size_t *outlen, size_t *outprespace);
	void (*reset)(CamelMimeFilter *f);
};

CamelType	      camel_mime_filter_get_type	(void);
CamelMimeFilter      *camel_mime_filter_new	(void);

void camel_mime_filter_filter(CamelMimeFilter *filter,
			      gchar *in, size_t len, size_t prespace,
			      gchar **out, size_t *outlen, size_t *outprespace);

void camel_mime_filter_complete(CamelMimeFilter *filter,
				gchar *in, size_t len, size_t prespace,
				gchar **out, size_t *outlen, size_t *outprespace);

void camel_mime_filter_reset(CamelMimeFilter *filter);

/* sets/returns number of bytes backed up on the input */
void camel_mime_filter_backup(CamelMimeFilter *filter, const gchar *data, size_t length);

/* ensure this much size available for filter output */
void camel_mime_filter_set_size(CamelMimeFilter *filter, size_t size, gint keep);

G_END_DECLS

#endif /* ! _CAMEL_MIME_FILTER_H */
