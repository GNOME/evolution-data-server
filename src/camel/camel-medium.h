/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 * SPDX-FileContributor: Bertrand Guiheneuf <bertrand@helixcode.com>
 * SPDX-FileContributor: Michael Zucchi <notzed@ximian.com>
 */

#if !defined (__CAMEL_H_INSIDE__) && !defined (CAMEL_COMPILATION)
#error "Only <camel/camel.h> can be included directly."
#endif

#ifndef CAMEL_MEDIUM_H
#define CAMEL_MEDIUM_H

#include <camel/camel-data-wrapper.h>
#include <camel/camel-name-value-array.h>

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

struct _CamelMedium {
	CamelDataWrapper parent;
	CamelMediumPrivate *priv;
};

struct _CamelMediumClass {
	CamelDataWrapperClass parent_class;

	void		(*add_header)		(CamelMedium *medium,
						 const gchar *name,
						 const gchar *value);
	void		(*set_header)		(CamelMedium *medium,
						 const gchar *name,
						 const gchar *value);
	void		(*remove_header)	(CamelMedium *medium,
						 const gchar *name);
	const gchar *	(*get_header)		(CamelMedium *medium,
						 const gchar *name);
	CamelNameValueArray *
			(*dup_headers)		(CamelMedium *medium);
	const CamelNameValueArray *
			(*get_headers)		(CamelMedium *medium);
	CamelDataWrapper *
			(*get_content)		(CamelMedium *medium);
	void		(*set_content)		(CamelMedium *medium,
						 CamelDataWrapper *content);

	/* Padding for future expansion */
	gpointer reserved[20];
};

GType		camel_medium_get_type		(void);
void		camel_medium_add_header		(CamelMedium *medium,
						 const gchar *name,
						 const gchar *value);
void		camel_medium_set_header		(CamelMedium *medium,
						 const gchar *name,
						 const gchar *value);
void		camel_medium_remove_header	(CamelMedium *medium,
						 const gchar *name);
const gchar *	camel_medium_get_header		(CamelMedium *medium,
						 const gchar *name);
CamelNameValueArray *
		camel_medium_dup_headers	(CamelMedium *medium);
const CamelNameValueArray *
		camel_medium_get_headers	(CamelMedium *medium);
CamelDataWrapper *
		camel_medium_get_content	(CamelMedium *medium);
void		camel_medium_set_content	(CamelMedium *medium,
						 CamelDataWrapper *content);

G_END_DECLS

#endif /* CAMEL_MEDIUM_H */
