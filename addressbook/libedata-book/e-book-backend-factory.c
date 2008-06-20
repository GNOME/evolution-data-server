/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *
 * Author:
 *   Chris Toshok (toshok@ximian.com)
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>

#include "e-book-backend-factory.h"

static void
e_book_backend_factory_instance_init (EBookBackendFactory *factory)
{
}

static void
e_book_backend_factory_class_init (EBookBackendFactoryClass *klass)
{
}

GType
e_book_backend_factory_get_type (void)
{
	static GType type = 0;

	if (! type) {
		GTypeInfo info = {
			sizeof (EBookBackendFactoryClass),
			NULL, /* base_class_init */
			NULL, /* base_class_finalize */
			(GClassInitFunc)  e_book_backend_factory_class_init,
			NULL, /* class_finalize */
			NULL, /* class_data */
			sizeof (EBookBackendFactory),
			0,    /* n_preallocs */
			(GInstanceInitFunc) e_book_backend_factory_instance_init
		};

		type = g_type_register_static (G_TYPE_OBJECT, "EBookBackendFactory", &info, 0);
	}

	return type;
}

/**
 * e_book_backend_factory_get_protocol:
 * @factory: an #EBookBackendFactory
 *
 * Gets the protocol that @factory creates backends for.
 *
 * Return value: A string representing a protocol.
 **/
const char*
e_book_backend_factory_get_protocol (EBookBackendFactory *factory)
{
	g_return_val_if_fail (E_IS_BOOK_BACKEND_FACTORY (factory), NULL);

	return E_BOOK_BACKEND_FACTORY_GET_CLASS (factory)->get_protocol (factory);
}

/**
 * e_book_backend_factory_new_backend:
 * @factory: an #EBookBackendFactory
 *
 * Creates a new #EBookBackend with @factory's protocol.
 *
 * Return value: A new #EBookBackend.
 **/
EBookBackend*
e_book_backend_factory_new_backend (EBookBackendFactory *factory)
{
	g_return_val_if_fail (E_IS_BOOK_BACKEND_FACTORY (factory), NULL);

	return E_BOOK_BACKEND_FACTORY_GET_CLASS (factory)->new_backend (factory);
}
