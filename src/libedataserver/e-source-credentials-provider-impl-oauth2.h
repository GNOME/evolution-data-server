/*
 * SPDX-FileCopyrightText: (C) 2015 Red Hat, Inc. (www.redhat.com)
 * SPDX-FileCopyrightText: (C) 2018 Red Hat, Inc. (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#if !defined (__LIBEDATASERVER_H_INSIDE__) && !defined (LIBEDATASERVER_COMPILATION)
#error "Only <libedataserver/libedataserver.h> should be included directly."
#endif

#ifndef E_SOURCE_CREDENTIALS_PROVIDER_IMPL_OAUTH2_H
#define E_SOURCE_CREDENTIALS_PROVIDER_IMPL_OAUTH2_H

#include <glib.h>
#include <glib-object.h>

#include <libedataserver/e-source.h>
#include <libedataserver/e-source-credentials-provider-impl.h>

/* Standard GObject macros */
#define E_TYPE_SOURCE_CREDENTIALS_PROVIDER_IMPL_OAUTH2 \
	(e_source_credentials_provider_impl_oauth2_get_type ())
#define E_SOURCE_CREDENTIALS_PROVIDER_IMPL_OAUTH2(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_SOURCE_CREDENTIALS_PROVIDER_IMPL_OAUTH2, ESourceCredentialsProviderImplOAuth2))
#define E_SOURCE_CREDENTIALS_PROVIDER_IMPL_OAUTH2_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_SOURCE_CREDENTIALS_PROVIDER_IMPL_OAUTH2, ESourceCredentialsProviderImplOAuth2Class))
#define E_IS_SOURCE_CREDENTIALS_PROVIDER_IMPL_OAUTH2(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_SOURCE_CREDENTIALS_PROVIDER_IMPL_OAUTH2))
#define E_IS_SOURCE_CREDENTIALS_PROVIDER_IMPL_OAUTH2_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_SOURCE_CREDENTIALS_PROVIDER_IMPL_OAUTH2))
#define E_SOURCE_CREDENTIALS_PROVIDER_IMPL_OAUTH2_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_SOURCE_CREDENTIALS_PROVIDER_IMPL_OAUTH2, ESourceCredentialsProviderImplOAuth2Class))

G_BEGIN_DECLS

typedef struct _ESourceCredentialsProviderImplOAuth2 ESourceCredentialsProviderImplOAuth2;
typedef struct _ESourceCredentialsProviderImplOAuth2Class ESourceCredentialsProviderImplOAuth2Class;
typedef struct _ESourceCredentialsProviderImplOAuth2Private ESourceCredentialsProviderImplOAuth2Private;

/**
 * ESourceCredentialsProviderImplOAuth2:
 *
 * OAuth2 based credentials provider implementation.
 *
 * Since: 3.28
 **/
struct _ESourceCredentialsProviderImplOAuth2 {
	/*< private >*/
	ESourceCredentialsProviderImpl parent;
	ESourceCredentialsProviderImplOAuth2Private *priv;
};

struct _ESourceCredentialsProviderImplOAuth2Class {
	ESourceCredentialsProviderImplClass parent_class;
};

GType		e_source_credentials_provider_impl_oauth2_get_type	(void);

G_END_DECLS

#endif /* E_SOURCE_CREDENTIALS_PROVIDER_IMPL_OAUTH2_H */
