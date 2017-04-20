/*
 * e-gdata-oauth2-authorizer.h
 *
 * This library is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef E_GDATA_OAUTH2_AUTHORIZER_H
#define E_GDATA_OAUTH2_AUTHORIZER_H

#include <gdata/gdata.h>
#include <libedataserver/libedataserver.h>

/* Standard GObject macros */
#define E_TYPE_GDATA_OAUTH2_AUTHORIZER \
	(e_gdata_oauth2_authorizer_get_type ())
#define E_GDATA_OAUTH2_AUTHORIZER(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_GDATA_OAUTH2_AUTHORIZER, EGDataOAuth2Authorizer))
#define E_GDATA_OAUTH2_AUTHORIZER_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_GDATA_OAUTH2_AUTHORIZER, EGDataOAuth2AuthorizerClass))
#define E_IS_GDATA_OAUTH2_AUTHORIZER(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_GDATA_OAUTH2_AUTHORIZER))
#define E_IS_GDATA_OAUTH2_AUTHORIZER_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_GDATA_OAUTH2_AUTHORIZER))
#define E_GDATA_OAUTH2_AUTHORIZER_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_GDATA_OAUTH2_AUTHORIZER, EGDataOAuth2AuthorizerClass))

G_BEGIN_DECLS

typedef struct _EGDataOAuth2Authorizer EGDataOAuth2Authorizer;
typedef struct _EGDataOAuth2AuthorizerClass EGDataOAuth2AuthorizerClass;
typedef struct _EGDataOAuth2AuthorizerPrivate EGDataOAuth2AuthorizerPrivate;

struct _EGDataOAuth2Authorizer {
	GObject parent;
	EGDataOAuth2AuthorizerPrivate *priv;
};

struct _EGDataOAuth2AuthorizerClass {
	GObjectClass parent_class;
};

GType		e_gdata_oauth2_authorizer_get_type
					(void) G_GNUC_CONST;
EGDataOAuth2Authorizer *
		e_gdata_oauth2_authorizer_new
					(ESource *source);
ESource *	e_gdata_oauth2_authorizer_ref_source
					(EGDataOAuth2Authorizer *authorizer);
void		e_gdata_oauth2_authorizer_set_credentials
					(EGDataOAuth2Authorizer *authorizer,
					 const ENamedParameters *credentials);
ENamedParameters *
		e_gdata_oauth2_authorizer_clone_credentials
					(EGDataOAuth2Authorizer *authorizer);

G_END_DECLS

#endif /* E_GDATA_OAUTH2_AUTHORIZER_H */
