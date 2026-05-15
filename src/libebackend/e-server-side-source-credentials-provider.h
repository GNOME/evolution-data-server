/*
 * SPDX-FileCopyrightText: (C) 2015 Red Hat, Inc. (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#if !defined (__LIBEBACKEND_H_INSIDE__) && !defined (LIBEBACKEND_COMPILATION)
#error "Only <libebackend/libebackend.h> should be included directly."
#endif

#ifndef E_SERVER_SIDE_SOURCE_CREDENTIALS_PROVIDER_H
#define E_SERVER_SIDE_SOURCE_CREDENTIALS_PROVIDER_H

#include <glib.h>
#include <glib-object.h>
#include <gio/gio.h>

#include <libedataserver/libedataserver.h>
#include <libebackend/e-source-registry-server.h>

/* Standard GObject macros */
#define E_TYPE_SERVER_SIDE_SOURCE_CREDENTIALS_PROVIDER \
	(e_server_side_source_credentials_provider_get_type ())
#define E_SERVER_SIDE_SOURCE_CREDENTIALS_PROVIDER(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_SERVER_SIDE_SOURCE_CREDENTIALS_PROVIDER, EServerSideSourceCredentialsProvider))
#define E_SERVER_SIDE_SOURCE_CREDENTIALS_PROVIDER_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_SERVER_SIDE_SOURCE_CREDENTIALS_PROVIDER, EServerSideSourceCredentialsProviderClass))
#define E_IS_SERVER_SIDE_SOURCE_CREDENTIALS_PROVIDER(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_SERVER_SIDE_SOURCE_CREDENTIALS_PROVIDER))
#define E_IS_SERVER_SIDE_SOURCE_CREDENTIALS_PROVIDER_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_SERVER_SIDE_SOURCE_CREDENTIALS_PROVIDER))
#define E_SERVER_SIDE_SOURCE_CREDENTIALS_PROVIDER_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_SERVER_SIDE_SOURCE_CREDENTIALS_PROVIDER, EServerSideSourceCredentialsProviderClass))

G_BEGIN_DECLS

typedef struct _EServerSideSourceCredentialsProvider EServerSideSourceCredentialsProvider;
typedef struct _EServerSideSourceCredentialsProviderClass EServerSideSourceCredentialsProviderClass;
typedef struct _EServerSideSourceCredentialsProviderPrivate EServerSideSourceCredentialsProviderPrivate;

/**
 * EServerSideSourceCredentialsProvider:
 * Since: 3.16
 **/
struct _EServerSideSourceCredentialsProvider {
	/*< private >*/
	ESourceCredentialsProvider parent;
	EServerSideSourceCredentialsProviderPrivate *priv;
};

struct _EServerSideSourceCredentialsProviderClass {
	ESourceCredentialsProviderClass parent_class;
};

GType		e_server_side_source_credentials_provider_get_type	(void) G_GNUC_CONST;
ESourceCredentialsProvider *
		e_server_side_source_credentials_provider_new		(ESourceRegistryServer *registry);
G_END_DECLS

#endif /* E_SERVER_SIDE_SOURCE_CREDENTIALS_PROVIDER_H */
