/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *
 * Author:
 *   Chris Toshok (toshok@ximian.com)
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 */

#include <config.h>

#include "e-book-backend.h"
#include "e-book-backend-factory.h"

G_DEFINE_ABSTRACT_TYPE (
	EBookBackendFactory,
	e_book_backend_factory,
	E_TYPE_BACKEND_FACTORY)

static const gchar *
book_backend_factory_get_hash_key (EBackendFactory *factory)
{
	EBookBackendFactoryClass *class;

	class = E_BOOK_BACKEND_FACTORY_GET_CLASS (factory);
	g_return_val_if_fail (class->factory_name != NULL, NULL);

	/* For address book backends the factory hash key is simply
	 * the factory name.  See ECalBackendFactory for a slightly
	 * more complex scheme. */

	return class->factory_name;
}

static EBackend *
book_backend_factory_new_backend (EBackendFactory *factory,
                                  ESource *source)
{
	EBookBackendFactoryClass *class;

	class = E_BOOK_BACKEND_FACTORY_GET_CLASS (factory);
	g_return_val_if_fail (g_type_is_a (
		class->backend_type, E_TYPE_BOOK_BACKEND), NULL);

	return g_object_new (class->backend_type, "source", source, NULL);
}

static void
e_book_backend_factory_class_init (EBookBackendFactoryClass *class)
{
	EBackendFactoryClass *factory_class;

	factory_class = E_BACKEND_FACTORY_CLASS (class);
	factory_class->get_hash_key = book_backend_factory_get_hash_key;
	factory_class->new_backend = book_backend_factory_new_backend;
}

static void
e_book_backend_factory_init (EBookBackendFactory *factory)
{
}
