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
			sizeof (EBookBackend),
			0,    /* n_preallocs */
			(GInstanceInitFunc) e_book_backend_factory_instance_init
		};

		type = g_type_register_static (G_TYPE_OBJECT, "EBookBackendFactory", &info, 0);
	}

	return type;
}

const char*
e_book_backend_factory_get_protocol (EBookBackendFactory *factory)
{
	g_return_val_if_fail (E_IS_BOOK_BACKEND_FACTORY (factory), NULL);

	return E_BOOK_BACKEND_FACTORY_GET_CLASS (factory)->get_protocol (factory);
}

EBookBackend*
e_book_backend_factory_new_backend (EBookBackendFactory *factory)
{
	g_return_val_if_fail (E_IS_BOOK_BACKEND_FACTORY (factory), NULL);

	return E_BOOK_BACKEND_FACTORY_GET_CLASS (factory)->new_backend (factory);
}
