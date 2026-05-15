/*
 * SPDX-FileCopyrightText: (C) 2015 Red Hat, Inc. (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#if !defined (__LIBEDATASERVER_H_INSIDE__) && !defined (LIBEDATASERVER_COMPILATION)
#error "Only <libedataserver/libedataserver.h> should be included directly."
#endif

#ifndef E_SOURCE_CREDENTIALS_PROVIDER_IMPL_H
#define E_SOURCE_CREDENTIALS_PROVIDER_IMPL_H

#include <glib.h>
#include <glib-object.h>

#include <libedataserver/e-extension.h>
#include <libedataserver/e-named-parameters.h>
#include <libedataserver/e-source.h>

/* Standard GObject macros */
#define E_TYPE_SOURCE_CREDENTIALS_PROVIDER_IMPL \
	(e_source_credentials_provider_impl_get_type ())
#define E_SOURCE_CREDENTIALS_PROVIDER_IMPL(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_SOURCE_CREDENTIALS_PROVIDER_IMPL, ESourceCredentialsProviderImpl))
#define E_SOURCE_CREDENTIALS_PROVIDER_IMPL_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_SOURCE_CREDENTIALS_PROVIDER_IMPL, ESourceCredentialsProviderImplClass))
#define E_IS_SOURCE_CREDENTIALS_PROVIDER_IMPL(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_SOURCE_CREDENTIALS_PROVIDER_IMPL))
#define E_IS_SOURCE_CREDENTIALS_PROVIDER_IMPL_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_SOURCE_CREDENTIALS_PROVIDER_IMPL))
#define E_SOURCE_CREDENTIALS_PROVIDER_IMPL_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_SOURCE_CREDENTIALS_PROVIDER_IMPL, ESourceCredentialsProviderImplClass))

G_BEGIN_DECLS

typedef struct _ESourceCredentialsProviderImpl ESourceCredentialsProviderImpl;
typedef struct _ESourceCredentialsProviderImplClass ESourceCredentialsProviderImplClass;
typedef struct _ESourceCredentialsProviderImplPrivate ESourceCredentialsProviderImplPrivate;

struct _ESourceCredentialsProvider;

/**
 * ESourceCredentialsProviderImpl:
 *
 * Credentials provider implementation base structure. The descendants
 * implement the virtual methods. The descendants are automatically
 * registered into an #ESourceCredentialsProvider.
 *
 * Since: 3.16
 **/
struct _ESourceCredentialsProviderImpl {
	/*< private >*/
	EExtension parent;
	ESourceCredentialsProviderImplPrivate *priv;
};

struct _ESourceCredentialsProviderImplClass {
	EExtensionClass parent_class;

	gboolean	(*can_process)		(ESourceCredentialsProviderImpl *provider_impl,
						 ESource *source);
	gboolean	(*can_store)		(ESourceCredentialsProviderImpl *provider_impl);
	gboolean	(*can_prompt)		(ESourceCredentialsProviderImpl *provider_impl);
	gboolean	(*lookup_sync)		(ESourceCredentialsProviderImpl *provider_impl,
						 ESource *source,
						 GCancellable *cancellable,
						 ENamedParameters **out_credentials,
						 GError **error);
	gboolean	(*store_sync)		(ESourceCredentialsProviderImpl *provider_impl,
						 ESource *source,
						 const ENamedParameters *credentials,
						 gboolean permanently,
						 GCancellable *cancellable,
						 GError **error);
	gboolean	(*delete_sync)		(ESourceCredentialsProviderImpl *provider_impl,
						 ESource *source,
						 GCancellable *cancellable,
						 GError **error);
};

GType		e_source_credentials_provider_impl_get_type	(void);
struct _ESourceCredentialsProvider *
		e_source_credentials_provider_impl_get_provider
							(ESourceCredentialsProviderImpl *provider_impl);
gboolean	e_source_credentials_provider_impl_can_process
							(ESourceCredentialsProviderImpl *provider_impl,
							 ESource *source);
gboolean	e_source_credentials_provider_impl_can_store
							(ESourceCredentialsProviderImpl *provider_impl);
gboolean	e_source_credentials_provider_impl_can_prompt
							(ESourceCredentialsProviderImpl *provider_impl);
gboolean	e_source_credentials_provider_impl_lookup_sync
							(ESourceCredentialsProviderImpl *provider_impl,
							 ESource *source,
							 GCancellable *cancellable,
							 ENamedParameters **out_credentials,
							 GError **error);
gboolean	e_source_credentials_provider_impl_store_sync
							(ESourceCredentialsProviderImpl *provider_impl,
							 ESource *source,
							 const ENamedParameters *credentials,
							 gboolean permanently,
							 GCancellable *cancellable,
							 GError **error);
gboolean	e_source_credentials_provider_impl_delete_sync
							(ESourceCredentialsProviderImpl *provider_impl,
							 ESource *source,
							 GCancellable *cancellable,
							 GError **error);

G_END_DECLS

#endif /* E_SOURCE_CREDENTIALS_PROVIDER_IMPL_H */
