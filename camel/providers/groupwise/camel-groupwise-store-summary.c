/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2002 Ximian Inc.
 *
 * Authors: Parthasarathi Susarla <sparthasarathi@novell.com>  
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */


#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <unistd.h>
#include <ctype.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>

#include "camel-groupwise-store-summary.h"

#include "camel-file-utils.h"

#include "libedataserver/md5-utils.h"
#include "libedataserver/e-memory.h"

#include "camel-private.h"
#include "camel-utf8.h"


#define CAMEL_GW_STORE_SUMMARY_VERSION (0)


static int summary_header_load(CamelStoreSummary *, FILE *);
static int summary_header_save(CamelStoreSummary *, FILE *);


static void camel_groupwise_store_summary_class_init (CamelGroupwiseStoreSummaryClass *klass);
static void camel_groupwise_store_summary_init       (CamelGroupwiseStoreSummary *obj);
static void camel_groupwise_store_summary_finalise   (CamelObject *obj);

static CamelStoreSummaryClass *camel_groupwise_store_summary_parent;


static void
camel_groupwise_store_summary_class_init (CamelGroupwiseStoreSummaryClass *klass)
{
	CamelStoreSummaryClass *ssklass = (CamelStoreSummaryClass *)klass;

	ssklass->summary_header_load = summary_header_load;
	ssklass->summary_header_save = summary_header_save;

}

static void
camel_groupwise_store_summary_init (CamelGroupwiseStoreSummary *s)
{

	((CamelStoreSummary *)s)->store_info_size = sizeof(CamelGroupwiseStoreInfo);
	s->version = CAMEL_GW_STORE_SUMMARY_VERSION;
}


static void
camel_groupwise_store_summary_finalise (CamelObject *obj)
{
}


CamelType
camel_groupwise_store_summary_get_type (void)
{
	static CamelType type = CAMEL_INVALID_TYPE;

	if (type == CAMEL_INVALID_TYPE) {
		camel_groupwise_store_summary_parent = (CamelStoreSummaryClass *)camel_store_summary_get_type();
		type = camel_type_register((CamelType)camel_groupwise_store_summary_parent, "CamelGroupwiseStoreSummary",
				sizeof (CamelGroupwiseStoreSummary),
				sizeof (CamelGroupwiseStoreSummaryClass),
				(CamelObjectClassInitFunc) camel_groupwise_store_summary_class_init,
				NULL,
				(CamelObjectInitFunc) camel_groupwise_store_summary_init,
				(CamelObjectFinalizeFunc) camel_groupwise_store_summary_finalise);
	}

	return type;
}


CamelGroupwiseStoreSummary *
camel_groupwise_store_summary_new (void)
{
	CamelGroupwiseStoreSummary *new = CAMEL_GW_STORE_SUMMARY ( camel_object_new (camel_groupwise_store_summary_get_type ()));

	return new;
}


static int
summary_header_load(CamelStoreSummary *s, FILE *in)
{
	return 0 ;
}


static int
summary_header_save(CamelStoreSummary *s, FILE *out)
{
	return 0 ;
}


