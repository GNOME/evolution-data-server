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

#include "e-book-backend-file-factory.h"
#include "e-book-backend-file.h"

static void
e_book_backend_file_factory_instance_init (EBookBackendFileFactory *factory)
{
}

static const char *
_get_protocol (EBookBackendFactory *factory)
{
	return "file";
}

static EBookBackend*
_new_backend (EBookBackendFactory *factory)
{
	return e_book_backend_file_new ();
}

static void
e_book_backend_file_factory_class_init (EBookBackendFileFactoryClass *klass)
{
  E_BOOK_BACKEND_FACTORY_CLASS (klass)->get_protocol = _get_protocol;
  E_BOOK_BACKEND_FACTORY_CLASS (klass)->new_backend = _new_backend;
}

static GType file_type;

static GType
e_book_backend_file_factory_get_type (GTypeModule *module)
{
	GType type;

	GTypeInfo info = {
		sizeof (EBookBackendFileFactoryClass),
		NULL, /* base_class_init */
		NULL, /* base_class_finalize */
		(GClassInitFunc)  e_book_backend_file_factory_class_init,
		NULL, /* class_finalize */
		NULL, /* class_data */
		sizeof (EBookBackend),
		0,    /* n_preallocs */
		(GInstanceInitFunc) e_book_backend_file_factory_instance_init
	};

	type = g_type_module_register_type (module,
					    E_TYPE_BOOK_BACKEND_FACTORY,
					    "EBookBackendFileFactory",
					    &info, 0);

	return type;
}



void
eds_module_initialize (GTypeModule *module)
{
	file_type = e_book_backend_file_factory_get_type (module);
}

void
eds_module_shutdown   (void)
{
}

void
eds_module_list_types (const GType **types, int *num_types)
{
	*types = &file_type;
	*num_types = 1;
}
