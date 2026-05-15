/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 * SPDX-FileContributor: Chris Toshok <toshok@ximian.com>
 */

#include "evolution-data-server-config.h"

#include "e-book-backend-ldap.h"

#define FACTORY_NAME "ldap"

typedef EBookBackendFactory EBookBackendLDAPFactory;
typedef EBookBackendFactoryClass EBookBackendLDAPFactoryClass;

static EModule *e_module;

/* Module Entry Points */
void e_module_load (GTypeModule *type_module);
void e_module_unload (GTypeModule *type_module);

/* Forward Declarations */
GType e_book_backend_ldap_factory_get_type (void);

G_DEFINE_DYNAMIC_TYPE (
	EBookBackendLDAPFactory,
	e_book_backend_ldap_factory,
	E_TYPE_BOOK_BACKEND_FACTORY)

static void
e_book_backend_ldap_factory_class_init (EBookBackendFactoryClass *class)
{
	EBackendFactoryClass *backend_factory_class;

	backend_factory_class = E_BACKEND_FACTORY_CLASS (class);
	backend_factory_class->e_module = e_module;
	backend_factory_class->share_subprocess = TRUE;

	class->factory_name = FACTORY_NAME;
	class->backend_type = E_TYPE_BOOK_BACKEND_LDAP;
}

static void
e_book_backend_ldap_factory_class_finalize (EBookBackendFactoryClass *class)
{
}

static void
e_book_backend_ldap_factory_init (EBookBackendFactory *factory)
{
}

G_MODULE_EXPORT void
e_module_load (GTypeModule *type_module)
{
	e_module = E_MODULE (type_module);

	e_book_backend_ldap_factory_register_type (type_module);
}

G_MODULE_EXPORT void
e_module_unload (GTypeModule *type_module)
{
	e_module = NULL;
}
