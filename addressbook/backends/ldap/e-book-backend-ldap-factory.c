/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *
 * Author:
 *   Chris Toshok (toshok@ximian.com)
 *
 * Copyright 2000, Ximian, Inc.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <pthread.h>
#include <string.h>

#include "e-book-backend-ldap-factory.h"
#include "e-book-backend-ldap.h"

static void
e_book_backend_ldap_factory_instance_init (EBookBackendLDAPFactory *factory)
{
}

static const char *
_get_protocol (EBookBackendFactory *factory)
{
	return "ldap";
}

static EBookBackend*
_new_backend (EBookBackendFactory *factory)
{
	return e_book_backend_ldap_new ();
}

static void
e_book_backend_ldap_factory_class_init (EBookBackendLDAPFactoryClass *klass)
{
	E_BOOK_BACKEND_FACTORY_CLASS (klass)->get_protocol = _get_protocol;
	E_BOOK_BACKEND_FACTORY_CLASS (klass)->new_backend = _new_backend;
}

static GType ldap_type;

static GType
e_book_backend_ldap_factory_get_type (GTypeModule *module)
{
	GType type;

	GTypeInfo info = {
		sizeof (EBookBackendLDAPFactoryClass),
		NULL, /* base_class_init */
		NULL, /* base_class_finalize */
		(GClassInitFunc)  e_book_backend_ldap_factory_class_init,
		NULL, /* class_finalize */
		NULL, /* class_data */
		sizeof (EBookBackend),
		0,    /* n_preallocs */
		(GInstanceInitFunc) e_book_backend_ldap_factory_instance_init
	};

	type = g_type_module_register_type (module,
					    E_TYPE_BOOK_BACKEND_FACTORY,
					    "EBookBackendLDAPFactory",
					    &info, 0);

	return type;
}



void
eds_module_initialize (GTypeModule *module)
{
	ldap_type = e_book_backend_ldap_factory_get_type (module);
}

void
eds_module_shutdown   (void)
{
}

void
eds_module_list_types (const GType **types, int *num_types)
{
	*types = &ldap_type;
	*num_types = 1;
}
