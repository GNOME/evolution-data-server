/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-medium.h : class for a medium object */

/*
 *
 * Authors:  Bertrand Guiheneuf <bertrand@helixcode.com>
 *	     Michael Zucchi <notzed@ximian.com>
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This library is free software you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#if !defined (__CAMEL_H_INSIDE__) && !defined (CAMEL_COMPILATION)
#error "Only <camel/camel.h> can be included directly."
#endif

#ifndef CAMEL_MEDIUM_H
#define CAMEL_MEDIUM_H

#include <camel/camel-data-wrapper.h>

/* Standard GObject macros */
#define CAMEL_TYPE_MEDIUM \
	(camel_medium_get_type ())
#define CAMEL_MEDIUM(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_MEDIUM, CamelMedium))
#define CAMEL_MEDIUM_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_MEDIUM, CamelMediumClass))
#define CAMEL_IS_MEDIUM(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_MEDIUM))
#define CAMEL_IS_MEDIUM_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_MEDIUM))
#define CAMEL_MEDIUM_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_MEDIUM, CamelMediumClass))

G_BEGIN_DECLS

typedef struct _CamelMedium CamelMedium;
typedef struct _CamelMediumClass CamelMediumClass;
typedef struct _CamelMediumPrivate CamelMediumPrivate;

typedef struct {
	const gchar *name;
	const gchar *value;
} CamelMediumHeader;

struct _CamelMedium {
	CamelDataWrapper parent;
	CamelMediumPrivate *priv;
};

struct _CamelMediumClass {
	CamelDataWrapperClass parent_class;

	void		(*add_header)		(CamelMedium *medium,
						 const gchar *name,
						 gconstpointer value);
	void		(*set_header)		(CamelMedium *medium,
						 const gchar *name,
						 gconstpointer value);
	void		(*remove_header)	(CamelMedium *medium,
						 const gchar *name);
	gconstpointer	(*get_header)		(CamelMedium *medium,
						 const gchar *name);
	GArray *	(*get_headers)		(CamelMedium *medium);
	void		(*free_headers)		(CamelMedium *medium,
						 GArray *headers);
	CamelDataWrapper *
			(*get_content)		(CamelMedium *medium);
	void		(*set_content)		(CamelMedium *medium,
						 CamelDataWrapper *content);
};

GType		camel_medium_get_type		(void);
void		camel_medium_add_header		(CamelMedium *medium,
						 const gchar *name,
						 gconstpointer value);
void		camel_medium_set_header		(CamelMedium *medium,
						 const gchar *name,
						 gconstpointer value);
void		camel_medium_remove_header	(CamelMedium *medium,
						 const gchar *name);
gconstpointer	camel_medium_get_header		(CamelMedium *medium,
						 const gchar *name);
GArray *	camel_medium_get_headers	(CamelMedium *medium);
void		camel_medium_free_headers	(CamelMedium *medium,
						 GArray *headers);
CamelDataWrapper *
		camel_medium_get_content	(CamelMedium *medium);
void		camel_medium_set_content	(CamelMedium *medium,
						 CamelDataWrapper *content);

G_END_DECLS

#endif /* CAMEL_MEDIUM_H */
