/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-multipart.h : class for a multipart */

/*
 *
 * Author :
 *  Bertrand Guiheneuf <bertrand@helixcode.com>
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

#if !defined (__CAMEL_H_INSIDE__) && !defined (CAMEL_COMPILATION)
#error "Only <camel/camel.h> can be included directly."
#endif

#ifndef CAMEL_MULTIPART_H
#define CAMEL_MULTIPART_H

#include <camel/camel-data-wrapper.h>

/* Standard GObject macros */
#define CAMEL_TYPE_MULTIPART \
	(camel_multipart_get_type ())
#define CAMEL_MULTIPART(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_MULTIPART, CamelMultipart))
#define CAMEL_MULTIPART_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_MULTIPART, CamelMultipartClass))
#define CAMEL_IS_MULTIPART(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_MULTIPART))
#define CAMEL_IS_MULTIPART_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_MULTIPART))
#define CAMEL_MULTIPART_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_MULTIPART, CamelMultipartClass))

G_BEGIN_DECLS

struct _CamelMimeParser;

typedef struct _CamelMultipart CamelMultipart;
typedef struct _CamelMultipartClass CamelMultipartClass;

struct _CamelMultipart {
	CamelDataWrapper parent;

	GList *parts;
	gchar *preface;
	gchar *postface;
};

struct _CamelMultipartClass {
	CamelDataWrapperClass parent_class;

	/* Virtual methods */
	void (*add_part) (CamelMultipart *multipart, CamelMimePart *part);
	void (*add_part_at) (CamelMultipart *multipart, CamelMimePart *part, guint index);
	void (*remove_part) (CamelMultipart *multipart, CamelMimePart *part);
	CamelMimePart * (*remove_part_at) (CamelMultipart *multipart, guint index);
	CamelMimePart * (*get_part) (CamelMultipart *multipart, guint index);
	guint (*get_number) (CamelMultipart *multipart);
	void (*set_boundary) (CamelMultipart *multipart, const gchar *boundary);
	const gchar * (*get_boundary) (CamelMultipart *multipart);

	gint (*construct_from_parser)(CamelMultipart *, struct _CamelMimeParser *);
	/*int (*construct_from_stream)(CamelMultipart *, CamelStream *);*/
};

GType camel_multipart_get_type (void);

/* public methods */
CamelMultipart *    camel_multipart_new            (void);
void                camel_multipart_add_part       (CamelMultipart *multipart,
						    CamelMimePart *part);
void                camel_multipart_add_part_at    (CamelMultipart *multipart,
						    CamelMimePart *part,
						    guint index);
void                camel_multipart_remove_part    (CamelMultipart *multipart,
						    CamelMimePart *part);
CamelMimePart *     camel_multipart_remove_part_at (CamelMultipart *multipart,
						    guint index);
CamelMimePart *     camel_multipart_get_part       (CamelMultipart *multipart,
						    guint index);
guint               camel_multipart_get_number     (CamelMultipart *multipart);
void                camel_multipart_set_boundary   (CamelMultipart *multipart,
						    const gchar *boundary);
const gchar *        camel_multipart_get_boundary   (CamelMultipart *multipart);

void		    camel_multipart_set_preface	   (CamelMultipart *multipart, const gchar *preface);
void		    camel_multipart_set_postface   (CamelMultipart *multipart, const gchar *postface);

gint		    camel_multipart_construct_from_parser(CamelMultipart *multipart, struct _CamelMimeParser *parser);

G_END_DECLS

#endif /* CAMEL_MULTIPART_H */
