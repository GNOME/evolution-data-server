/*
 * SPDX-FileCopyrightText: (C) 2015 Red Hat, Inc. (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#if !defined (__LIBEDATASERVER_H_INSIDE__) && !defined (LIBEDATASERVER_COMPILATION)
#error "Only <libedataserver/libedataserver.h> should be included directly."
#endif

#ifndef E_SOURCE_CREDENTIALS_PROVIDER_IMPL_PASSWORD_H
#define E_SOURCE_CREDENTIALS_PROVIDER_IMPL_PASSWORD_H

#include <glib.h>
#include <glib-object.h>

#include <libedataserver/e-source-credentials-provider-impl.h>

/* Standard GObject macros */
#define E_TYPE_SOURCE_CREDENTIALS_PROVIDER_IMPL_PASSWORD \
	(e_source_credentials_provider_impl_password_get_type ())
#define E_SOURCE_CREDENTIALS_PROVIDER_IMPL_PASSWORD(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_SOURCE_CREDENTIALS_PROVIDER_IMPL_PASSWORD, ESourceCredentialsProviderImplPassword))
#define E_SOURCE_CREDENTIALS_PROVIDER_IMPL_PASSWORD_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_SOURCE_CREDENTIALS_PROVIDER_IMPL_PASSWORD, ESourceCredentialsProviderImplPasswordClass))
#define E_IS_SOURCE_CREDENTIALS_PROVIDER_IMPL_PASSWORD(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_SOURCE_CREDENTIALS_PROVIDER_IMPL_PASSWORD))
#define E_IS_SOURCE_CREDENTIALS_PROVIDER_IMPL_PASSWORD_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_SOURCE_CREDENTIALS_PROVIDER_IMPL_PASSWORD))
#define E_SOURCE_CREDENTIALS_PROVIDER_IMPL_PASSWORD_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_SOURCE_CREDENTIALS_PROVIDER_IMPL_PASSWORD, ESourceCredentialsProviderImplPasswordClass))

G_BEGIN_DECLS

typedef struct _ESourceCredentialsProviderImplPassword ESourceCredentialsProviderImplPassword;
typedef struct _ESourceCredentialsProviderImplPasswordClass ESourceCredentialsProviderImplPasswordClass;
typedef struct _ESourceCredentialsProviderImplPasswordPrivate ESourceCredentialsProviderImplPasswordPrivate;

/**
 * ESourceCredentialsProviderImplPassword:
 *
 * Password based credentials provider implementation.
 *
 * Since: 3.16
 **/
struct _ESourceCredentialsProviderImplPassword {
	/*< private >*/
	ESourceCredentialsProviderImpl parent;
	ESourceCredentialsProviderImplPasswordPrivate *priv;
};

struct _ESourceCredentialsProviderImplPasswordClass {
	ESourceCredentialsProviderImplClass parent_class;
};

GType		e_source_credentials_provider_impl_password_get_type	(void);

G_END_DECLS

#endif /* E_SOURCE_CREDENTIALS_PROVIDER_IMPL_PASSWORD_H */
