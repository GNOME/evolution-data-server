/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * camel-signed--multipart.h : class for a signed-multipart
 *
 * Authors: Michael Zucchi <notzed@ximian.com>
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301
 * USA
 */

/* Should this be a subclass of multipart?
   No, because we dont have different parts?
   I'm not really sure yet ... ? */

#if !defined (__CAMEL_H_INSIDE__) && !defined (CAMEL_COMPILATION)
#error "Only <camel/camel.h> can be included directly."
#endif

#ifndef CAMEL_MULTIPART_SIGNED_H
#define CAMEL_MULTIPART_SIGNED_H

#include <camel/camel-multipart.h>

/* Standard GObject macros */
#define CAMEL_TYPE_MULTIPART_SIGNED \
	(camel_multipart_signed_get_type ())
#define CAMEL_MULTIPART_SIGNED(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_MULTIPART_SIGNED, CamelMultipartSigned))
#define CAMEL_MULTIPART_SIGNED_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_MULTIPART_SIGNED, CamelMultipartSignedClass))
#define CAMEL_IS_MULTIPART_SIGNED(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_MULTIPART_SIGNED))
#define CAMEL_IS_MULTIPART_SIGNED_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_MULTIPART_SIGNED))
#define CAMEL_MULTIPART_SIGNED_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_MULTIPART_SIGNED, CamelMultipartSignedClass))

G_BEGIN_DECLS

/*
enum {
	CAMEL_MULTIPART_EMPTY,
	CAMEL_MULTIPART_CONST,
	CAMEL_MULTIPART_SIGN,
	CAMEL_MULTIPART_ENCR,
};
*/

/* 'handy' enums for getting the internal parts of the multipart */
enum {
	CAMEL_MULTIPART_SIGNED_CONTENT,
	CAMEL_MULTIPART_SIGNED_SIGNATURE
};

typedef struct _CamelMultipartSigned CamelMultipartSigned;

struct _CamelMultipartSigned
{
	CamelMultipart parent;

	/* these are the client visible parts, decoded forms of our data wrapper content */
	CamelMimePart *content;
	CamelMimePart *signature;

	/* the raw content which must go over the wire, if we have generated it */
	/* perhaps this should jsut set data_wrapper->stream and update start1/end1 accordingly, as it is done
	   for other parts, or visa versa? */
	CamelStream *contentraw;

	/*int state;*/

	/* just cache some info we use */
	gchar *protocol;
	gchar *micalg;

	/* offset pointers of start of boundary in content object */
	off_t start1, end1;
	off_t start2, end2;
};

typedef struct {
	CamelMultipartClass parent_class;
} CamelMultipartSignedClass;

GType camel_multipart_signed_get_type (void);

/* public methods */
CamelMultipartSigned *camel_multipart_signed_new           (void);

CamelStream *camel_multipart_signed_get_content_stream(CamelMultipartSigned *mps, CamelException *ex);

G_END_DECLS

#endif /* CAMEL_MULTIPART_SIGNED_H */
