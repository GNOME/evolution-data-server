/*
 * e-gdata-goa-authorizer.h
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with the program; if not, see <http://www.gnu.org/licenses/>
 *
 */

#ifndef E_GDATA_GOA_AUTHORIZER_H
#define E_GDATA_GOA_AUTHORIZER_H

#include <gdata/gdata.h>

#define GOA_API_IS_SUBJECT_TO_CHANGE
#include <goa/goa.h>

/* Standard GObject macros */
#define E_TYPE_GDATA_GOA_AUTHORIZER \
	(e_gdata_goa_authorizer_get_type ())
#define E_GDATA_GOA_AUTHORIZER(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_GDATA_GOA_AUTHORIZER, EGDataGoaAuthorizer))
#define E_GDATA_GOA_AUTHORIZER_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_GDATA_GOA_AUTHORIZER, EGDataGoaAuthorizerClass))
#define E_IS_GDATA_GOA_AUTHORIZER(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_GDATA_GOA_AUTHORIZER))
#define E_IS_GDATA_GOA_AUTHORIZER_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_GDATA_GOA_AUTHORIZER))
#define E_GDATA_GOA_AUTHORIZER_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_GDATA_GOA_AUTHORIZER, EGDataGoaAuthorizerClass))

G_BEGIN_DECLS

typedef struct _EGDataGoaAuthorizer EGDataGoaAuthorizer;
typedef struct _EGDataGoaAuthorizerClass EGDataGoaAuthorizerClass;
typedef struct _EGDataGoaAuthorizerPrivate EGDataGoaAuthorizerPrivate;

struct _EGDataGoaAuthorizer {
	GObject parent;
	EGDataGoaAuthorizerPrivate *priv;
};

struct _EGDataGoaAuthorizerClass {
	GObjectClass parent_class;
};

GType		e_gdata_goa_authorizer_get_type (void);
EGDataGoaAuthorizer *
		e_gdata_goa_authorizer_new
					(GoaObject *goa_object);
GoaObject *	e_gdata_goa_authorizer_get_goa_object
					(EGDataGoaAuthorizer *authorizer);

#endif /* E_GDATA_GOA_AUTHORIZER_H */
