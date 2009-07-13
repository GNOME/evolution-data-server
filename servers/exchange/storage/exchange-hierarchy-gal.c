/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/* Copyright (C) 2002-2004 Novell, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

/* ExchangeHierarchyGAL: class for the Global Address List hierarchy
 * of an Exchange storage. (Currently the "hierarchy" only contains
 * a single folder, but see bugzilla #21029.)
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "exchange-hierarchy-gal.h"
#include "exchange-account.h"
#include "e-folder-exchange.h"
#include "exchange-esource.h"

#include <libedataserver/e-source-list.h>

#define PARENT_TYPE EXCHANGE_TYPE_HIERARCHY

E2K_MAKE_TYPE (exchange_hierarchy_gal, ExchangeHierarchyGAL, NULL, NULL, PARENT_TYPE)

ExchangeHierarchy *
exchange_hierarchy_gal_new (ExchangeAccount *account,
			    const gchar *hierarchy_name,
			    const gchar *physical_uri_prefix)
{
	ExchangeHierarchy *hier;
	EFolder *toplevel;

	g_return_val_if_fail (EXCHANGE_IS_ACCOUNT (account), NULL);
	g_return_val_if_fail (hierarchy_name != NULL, NULL);
	g_return_val_if_fail (physical_uri_prefix != NULL, NULL);

	hier = g_object_new (EXCHANGE_TYPE_HIERARCHY_GAL, NULL);

	toplevel = e_folder_exchange_new (hier, hierarchy_name,
					  "contacts/ldap", NULL,
					  physical_uri_prefix,
					  physical_uri_prefix);
	exchange_hierarchy_construct (hier, account,
				      EXCHANGE_HIERARCHY_GAL, toplevel,
				      NULL, NULL, NULL);
	/* Add ESource */
	add_folder_esource (hier->account, EXCHANGE_CONTACTS_FOLDER,
			    hierarchy_name, physical_uri_prefix);

	g_object_unref (toplevel);

	return hier;
}
