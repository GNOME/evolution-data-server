/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Authors: Bertrand Guiheneuf <bertrand@helixcode.com>
 *           Michael Zucchi <notzed@ximian.com>
 *           Jeffrey Stedfast <fejj@ximian.com>
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

#ifndef CAMEL_DATA_WRAPPER_H
#define CAMEL_DATA_WRAPPER_H 1

#include <glib.h>
#include <sys/types.h>
#include <camel/camel-object.h>
#include <camel/camel-mime-utils.h>

#define CAMEL_DATA_WRAPPER_TYPE     (camel_data_wrapper_get_type ())
#define CAMEL_DATA_WRAPPER(obj)     (CAMEL_CHECK_CAST((obj), CAMEL_DATA_WRAPPER_TYPE, CamelDataWrapper))
#define CAMEL_DATA_WRAPPER_CLASS(k) (CAMEL_CHECK_CLASS_CAST ((k), CAMEL_DATA_WRAPPER_TYPE, CamelDataWrapperClass))
#define CAMEL_IS_DATA_WRAPPER(o)    (CAMEL_CHECK_TYPE((o), CAMEL_DATA_WRAPPER_TYPE))

G_BEGIN_DECLS

struct _CamelDataWrapper {
	CamelObject parent_object;
	struct _CamelDataWrapperPrivate *priv;

	CamelTransferEncoding encoding;

	CamelContentType *mime_type;
	CamelStream *stream;

	guint offline:1;
};

typedef struct {
	CamelObjectClass parent_class;

	/* Virtual methods */
	void                (*set_mime_type)          (CamelDataWrapper *data_wrapper,
						       const gchar *mime_type);
	gchar *              (*get_mime_type)          (CamelDataWrapper *data_wrapper);
	CamelContentType *  (*get_mime_type_field)    (CamelDataWrapper *data_wrapper);
	void                (*set_mime_type_field)    (CamelDataWrapper *data_wrapper,
						       CamelContentType *mime_type_field);

	gssize             (*write_to_stream)        (CamelDataWrapper *data_wrapper,
						       CamelStream *stream);

	gssize             (*decode_to_stream)       (CamelDataWrapper *data_wrapper,
						       CamelStream *stream);

	gint                 (*construct_from_stream)  (CamelDataWrapper *data_wrapper,
						       CamelStream *);

	gboolean            (*is_offline)             (CamelDataWrapper *data_wrapper);
} CamelDataWrapperClass;

/* Standard Camel function */
CamelType camel_data_wrapper_get_type (void);

/* public methods */
CamelDataWrapper *camel_data_wrapper_new(void);
gssize           camel_data_wrapper_write_to_stream        (CamelDataWrapper *data_wrapper,
							     CamelStream *stream);
gssize           camel_data_wrapper_decode_to_stream       (CamelDataWrapper *data_wrapper,
							     CamelStream *stream);

void              camel_data_wrapper_set_mime_type          (CamelDataWrapper *data_wrapper,
							     const gchar *mime_type);
gchar             *camel_data_wrapper_get_mime_type          (CamelDataWrapper *data_wrapper);
CamelContentType *camel_data_wrapper_get_mime_type_field    (CamelDataWrapper *data_wrapper);
void              camel_data_wrapper_set_mime_type_field    (CamelDataWrapper *data_wrapper,
							     CamelContentType *mime_type);

gint               camel_data_wrapper_construct_from_stream  (CamelDataWrapper *data_wrapper,
							     CamelStream *stream);

gboolean          camel_data_wrapper_is_offline             (CamelDataWrapper *data_wrapper);

G_END_DECLS

#endif /* CAMEL_DATA_WRAPPER_H */
