/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 * SPDX-FileContributor: Michael Zucchi <notzed@ximian.com>
 */

/* Should this be a subclass of multipart?
 * No, because we don't have different parts?
 * I'm not really sure yet ... ? */

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

/* 'handy' enums for getting the internal parts of the multipart */
enum {
	CAMEL_MULTIPART_SIGNED_CONTENT,
	CAMEL_MULTIPART_SIGNED_SIGNATURE
};

typedef struct _CamelMultipartSigned CamelMultipartSigned;
typedef struct _CamelMultipartSignedClass CamelMultipartSignedClass;
typedef struct _CamelMultipartSignedPrivate CamelMultipartSignedPrivate;

struct _CamelMultipartSigned {
	CamelMultipart parent;
	CamelMultipartSignedPrivate *priv;
};

struct _CamelMultipartSignedClass {
	CamelMultipartClass parent_class;

	/* Padding for future expansion */
	gpointer reserved[20];
};

GType		camel_multipart_signed_get_type (void) G_GNUC_CONST;
CamelMultipartSigned *
		camel_multipart_signed_new	(void);
CamelStream *	camel_multipart_signed_get_content_stream
						(CamelMultipartSigned *mps,
						 GError **error);
void		camel_multipart_signed_set_content_stream
						(CamelMultipartSigned *mps,
						 CamelStream *content_stream);
void		camel_multipart_signed_set_signature
						(CamelMultipartSigned *mps,
						 CamelMimePart *signature);

G_END_DECLS

#endif /* CAMEL_MULTIPART_SIGNED_H */
