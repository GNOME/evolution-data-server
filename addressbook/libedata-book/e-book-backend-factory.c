/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *
 * Author:
 *   Chris Toshok (toshok@ximian.com)
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 */

/**
 * SECTION: e-book-backend-factory
 * @include: libedata-book/libedata-book.h
 * @short_description: The factory for creating new addressbooks
 *
 * This class handles creation of new addressbooks of various
 * backend types.
 **/

#include <config.h>
#include <string.h>

#include "e-book-backend.h"
#include "e-book-backend-factory.h"
#include "e-data-book-factory.h"

G_DEFINE_ABSTRACT_TYPE (
	EBookBackendFactory,
	e_book_backend_factory,
	E_TYPE_BACKEND_FACTORY)

static EDataBookFactory *
book_backend_factory_get_data_factory (EBackendFactory *factory)
{
	EExtensible *extensible;

	extensible = e_extension_get_extensible (E_EXTENSION (factory));

	return E_DATA_BOOK_FACTORY (extensible);
}

static const gchar *
book_backend_factory_get_hash_key (EBackendFactory *factory)
{
	EBookBackendFactoryClass *class;
	const gchar *component_name;
	gchar *hash_key;
	gsize length;

	class = E_BOOK_BACKEND_FACTORY_GET_CLASS (factory);
	g_return_val_if_fail (class->factory_name != NULL, NULL);

	component_name = E_SOURCE_EXTENSION_ADDRESS_BOOK;

	/* Hash Key: FACTORY_NAME ':' COMPONENT_NAME */
	length = strlen (class->factory_name) + strlen (component_name) + 2;
	hash_key = g_alloca (length);
	g_snprintf (
		hash_key, length, "%s:%s",
		class->factory_name, component_name);

	return g_intern_string (hash_key);
}

static EBackend *
book_backend_factory_new_backend (EBackendFactory *factory,
                                  ESource *source)
{
	EBookBackendFactoryClass *class;
	EDataBookFactory *data_factory;
	ESourceRegistry *registry;

	class = E_BOOK_BACKEND_FACTORY_GET_CLASS (factory);
	g_return_val_if_fail (g_type_is_a (
		class->backend_type, E_TYPE_BOOK_BACKEND), NULL);

	data_factory = book_backend_factory_get_data_factory (factory);
	registry = e_data_factory_get_registry (E_DATA_FACTORY (data_factory));

	return g_object_new (
		class->backend_type,
		"registry", registry,
		"source", source, NULL);
}

static void
e_book_backend_factory_class_init (EBookBackendFactoryClass *class)
{
	EExtensionClass *extension_class;
	EBackendFactoryClass *factory_class;

	extension_class = E_EXTENSION_CLASS (class);
	extension_class->extensible_type = E_TYPE_DATA_BOOK_FACTORY;

	factory_class = E_BACKEND_FACTORY_CLASS (class);
	factory_class->get_hash_key = book_backend_factory_get_hash_key;
	factory_class->new_backend = book_backend_factory_new_backend;
}

static void
e_book_backend_factory_init (EBookBackendFactory *factory)
{
}
