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

#include <libedataserver/e-data-server-module.h>
#include "libedata-book/e-book-backend-factory.h"
#include "e-book-backend-file.h"

E_BOOK_BACKEND_FACTORY_SIMPLE (file, File, e_book_backend_file_new)

static GType  file_type;

void
eds_module_initialize (GTypeModule *module)
{
	file_type = _file_factory_get_type (module);
}

void
eds_module_shutdown   (void)
{
}

void
eds_module_list_types (const GType **types, int *num_types)
{
	*types = & file_type;
	*num_types = 1;
}

