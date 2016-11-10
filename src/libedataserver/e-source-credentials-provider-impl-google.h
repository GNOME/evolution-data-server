/*
 * Copyright (C) 2015 Red Hat, Inc. (www.redhat.com)
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

#if !defined (__LIBEDATASERVER_H_INSIDE__) && !defined (LIBEDATASERVER_COMPILATION)
#error "Only <libedataserver/libedataserver.h> should be included directly."
#endif

#ifndef E_SOURCE_CREDENTIALS_PROVIDER_IMPL_GOOGLE_H
#define E_SOURCE_CREDENTIALS_PROVIDER_IMPL_GOOGLE_H

#include <glib.h>
#include <glib-object.h>

#include <libedataserver/e-source.h>
#include <libedataserver/e-source-credentials-provider-impl.h>

/* Standard GObject macros */
#define E_TYPE_SOURCE_CREDENTIALS_PROVIDER_IMPL_GOOGLE \
	(e_source_credentials_provider_impl_google_get_type ())
#define E_SOURCE_CREDENTIALS_PROVIDER_IMPL_GOOGLE(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_SOURCE_CREDENTIALS_PROVIDER_IMPL_GOOGLE, ESourceCredentialsProviderImplGoogle))
#define E_SOURCE_CREDENTIALS_PROVIDER_IMPL_GOOGLE_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_SOURCE_CREDENTIALS_PROVIDER_IMPL_GOOGLE, ESourceCredentialsProviderImplGoogleClass))
#define E_IS_SOURCE_CREDENTIALS_PROVIDER_IMPL_GOOGLE(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_SOURCE_CREDENTIALS_PROVIDER_IMPL_GOOGLE))
#define E_IS_SOURCE_CREDENTIALS_PROVIDER_IMPL_GOOGLE_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_SOURCE_CREDENTIALS_PROVIDER_IMPL_GOOGLE))
#define E_SOURCE_CREDENTIALS_PROVIDER_IMPL_GOOGLE_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_SOURCE_CREDENTIALS_PROVIDER_IMPL_GOOGLE, ESourceCredentialsProviderImplGoogleClass))

/* The key names used in crendetials ENamedParameters structure */
#define E_SOURCE_CREDENTIAL_GOOGLE_SECRET "Google-secret"

/* Secret key names, saved by the code; not the names returned by the Google server */
#define E_GOOGLE_SECRET_REFRESH_TOKEN "refresh_token"
#define E_GOOGLE_SECRET_ACCESS_TOKEN "access_token"
#define E_GOOGLE_SECRET_EXPIRES_AFTER "expires_after"

G_BEGIN_DECLS

typedef struct _ESourceCredentialsProviderImplGoogle ESourceCredentialsProviderImplGoogle;
typedef struct _ESourceCredentialsProviderImplGoogleClass ESourceCredentialsProviderImplGoogleClass;
typedef struct _ESourceCredentialsProviderImplGooglePrivate ESourceCredentialsProviderImplGooglePrivate;

/**
 * ESourceCredentialsProviderImplGoogle:
 *
 * Google based credentials provider implementation.
 *
 * Since: 3.20
 **/
struct _ESourceCredentialsProviderImplGoogle {
	/*< private >*/
	ESourceCredentialsProviderImpl parent;
	ESourceCredentialsProviderImplGooglePrivate *priv;
};

struct _ESourceCredentialsProviderImplGoogleClass {
	ESourceCredentialsProviderImplClass parent_class;
};

GType		e_source_credentials_provider_impl_google_get_type	(void);

gboolean	e_source_credentials_google_is_supported		(void);
gboolean	e_source_credentials_google_get_access_token_sync	(ESource *source,
									 const ENamedParameters *credentials,
									 gchar **out_access_token,
									 gint *out_expires_in_seconds,
									 GCancellable *cancellable,
									 GError **error);
gboolean	e_source_credentials_google_util_generate_secret_uid	(ESource *source,
									 gchar **out_uid);
gboolean	e_source_credentials_google_util_encode_to_secret	(gchar **out_secret,
									 const gchar *key1_name,
									 const gchar *value1,
									 ...) G_GNUC_NULL_TERMINATED;
gboolean	e_source_credentials_google_util_decode_from_secret	(const gchar *secret,
									 const gchar *key1_name,
									 gchar **out_value1,
									 ...) G_GNUC_NULL_TERMINATED;
gboolean	e_source_credentials_google_util_extract_from_credentials
									(const ENamedParameters *credentials,
									 gchar **out_access_token,
									 gint *out_expires_in_seconds);
G_END_DECLS

#endif /* E_SOURCE_CREDENTIALS_PROVIDER_IMPL_GOOGLE_H */
