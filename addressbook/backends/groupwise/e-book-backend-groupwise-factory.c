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

#include <libedataserver/e-data-server-module.h>
#include "libedata-book/e-book-backend-factory.h"
#include "e-book-backend-groupwise.h"

E_BOOK_BACKEND_FACTORY_SIMPLE (groupwise, Groupwise, e_book_backend_groupwise_new)

static GType  groupwise_type;

void
eds_module_initialize (GTypeModule *module)
{
	groupwise_type = _groupwise_factory_get_type (module);
}

void
eds_module_shutdown   (void)
{
}

void
eds_module_list_types (const GType **types, int *num_types)
{
	*types = & groupwise_type;
	*num_types = 1;
}

