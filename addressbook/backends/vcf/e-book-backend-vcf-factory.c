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
#include "e-book-backend-vcf.h"

E_BOOK_BACKEND_FACTORY_SIMPLE (vcf, VCF, e_book_backend_vcf_new)

static GType  vcf_type;

void
eds_module_initialize (GTypeModule *module)
{
	vcf_type = _vcf_factory_get_type (module);
}

void
eds_module_shutdown   (void)
{
}

void
eds_module_list_types (const GType **types, int *num_types)
{
	*types = & vcf_type;
	*num_types = 1;
}

