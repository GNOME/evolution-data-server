/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-medium.h : class for a medium object */

/*
 *
 * Authors:  Bertrand Guiheneuf <bertrand@helixcode.com>
 *	     Michael Zucchi <notzed@ximian.com>
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

#ifndef CAMEL_MEDIUM_H
#define CAMEL_MEDIUM_H 1

#include <camel/camel-data-wrapper.h>

#define CAMEL_MEDIUM_TYPE     (camel_medium_get_type ())
#define CAMEL_MEDIUM(obj)     (CAMEL_CHECK_CAST((obj), CAMEL_MEDIUM_TYPE, CamelMedium))
#define CAMEL_MEDIUM_CLASS(k) (CAMEL_CHECK_CLASS_CAST ((k), CAMEL_MEDIUM_TYPE, CamelMediumClass))
#define CAMEL_IS_MEDIUM(o)    (CAMEL_CHECK_TYPE((o), CAMEL_MEDIUM_TYPE))

G_BEGIN_DECLS

typedef struct {
	const gchar *name;
	const gchar *value;
} CamelMediumHeader;

struct _CamelMedium {
	CamelDataWrapper parent_object;

	/* The content of the medium, as opposed to our parent
	 * CamelDataWrapper, which wraps both the headers and the
	 * content.
	 */
	CamelDataWrapper *content;
};

typedef struct {
	CamelDataWrapperClass parent_class;

	/* Virtual methods */
	void  (*add_header) (CamelMedium *medium, const gchar *name, gconstpointer value);
	void  (*set_header) (CamelMedium *medium, const gchar *name, gconstpointer value);
	void  (*remove_header) (CamelMedium *medium, const gchar *name);
	gconstpointer  (*get_header) (CamelMedium *medium,  const gchar *name);

	GArray * (*get_headers) (CamelMedium *medium);
	void (*free_headers) (CamelMedium *medium, GArray *headers);

	CamelDataWrapper * (*get_content_object) (CamelMedium *medium);
	void (*set_content_object) (CamelMedium *medium, CamelDataWrapper *content);
} CamelMediumClass;

/* Standard Camel function */
CamelType camel_medium_get_type (void);

/* Header get/set interface */
void camel_medium_add_header (CamelMedium *medium, const gchar *name, gconstpointer value);
void camel_medium_set_header (CamelMedium *medium, const gchar *name, gconstpointer value);
void camel_medium_remove_header (CamelMedium *medium, const gchar *name);
gconstpointer camel_medium_get_header (CamelMedium *medium, const gchar *name);

GArray *camel_medium_get_headers (CamelMedium *medium);
void camel_medium_free_headers (CamelMedium *medium, GArray *headers);

/* accessor methods */
CamelDataWrapper *camel_medium_get_content_object (CamelMedium *medium);
void camel_medium_set_content_object (CamelMedium *medium,
				      CamelDataWrapper *content);

G_END_DECLS

#endif /* CAMEL_MEDIUM_H */
