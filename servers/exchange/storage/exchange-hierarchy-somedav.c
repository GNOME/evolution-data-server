/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/* Copyright (C) 2002-2004 Novell, Inc.
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

/* ExchangeHierarchySomeDAV: class for a hierarchy consisting of a
 * specific group of WebDAV folders
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "exchange-hierarchy-somedav.h"
#include "exchange-account.h"
#include "e-folder-exchange.h"
#include "e2k-propnames.h"
#include "e2k-marshal.h"
#include "e2k-uri.h"
#include "e2k-utils.h"

#include <libedataserver/e-xml-hash-utils.h>

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct _ExchangeHierarchySomeDAVPrivate {
	gboolean scanned;
};

enum {
	HREF_UNREADABLE,
	LAST_SIGNAL
};

static guint signals [LAST_SIGNAL] = { 0 };

#define PARENT_TYPE EXCHANGE_TYPE_HIERARCHY_WEBDAV
static ExchangeHierarchyWebDAVClass *parent_class = NULL;

static ExchangeAccountFolderResult scan_subtree (ExchangeHierarchy *hier,
						 EFolder *folder,
						 gboolean offline);
static void finalize (GObject *object);

static void
class_init (GObjectClass *object_class)
{
	ExchangeHierarchyClass *exchange_hierarchy_class =
		EXCHANGE_HIERARCHY_CLASS (object_class);

	parent_class = g_type_class_ref (PARENT_TYPE);

	/* virtual method override */
	object_class->finalize = finalize;

	exchange_hierarchy_class->scan_subtree   = scan_subtree;

	/* signals */
	signals[HREF_UNREADABLE] =
		g_signal_new ("href_unreadable",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (ExchangeHierarchySomeDAVClass, href_unreadable),
			      NULL, NULL,
			      e2k_marshal_NONE__STRING,
			      G_TYPE_NONE, 1,
			      G_TYPE_STRING);
}

static void
init (GObject *object)
{
	ExchangeHierarchySomeDAV *hsd = EXCHANGE_HIERARCHY_SOMEDAV (object);

	hsd->priv = g_new0 (ExchangeHierarchySomeDAVPrivate, 1);
}

static void
finalize (GObject *object)
{
	ExchangeHierarchySomeDAV *hsd = EXCHANGE_HIERARCHY_SOMEDAV (object);

	g_free (hsd->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

E2K_MAKE_TYPE (exchange_hierarchy_somedav, ExchangeHierarchySomeDAV, class_init, init, PARENT_TYPE)


static inline gboolean
folder_is_unreadable (E2kProperties *props)
{
	char *access;

	access = e2k_properties_get_prop (props, PR_ACCESS);
	return !access || !atoi (access);
}

static const char *folder_props[] = {
	E2K_PR_EXCHANGE_FOLDER_CLASS,
	E2K_PR_HTTPMAIL_UNREAD_COUNT,
	E2K_PR_DAV_DISPLAY_NAME,
	PR_ACCESS
};
static const int n_folder_props = sizeof (folder_props) / sizeof (folder_props[0]);

static ExchangeAccountFolderResult
scan_subtree (ExchangeHierarchy *hier, EFolder *folder, gboolean offline)
{
	ExchangeHierarchySomeDAV *hsd = EXCHANGE_HIERARCHY_SOMEDAV (hier);
	GPtrArray *hrefs;
	E2kResultIter *iter;
	E2kResult *result;
	int folders_returned=0, folders_added=0, i, mode;
	E2kHTTPStatus status;
	ExchangeAccountFolderResult folder_result;
	EFolder *iter_folder = NULL;

	if (hsd->priv->scanned || folder != hier->toplevel)
		return EXCHANGE_ACCOUNT_FOLDER_OK;
	hsd->priv->scanned = TRUE;

	if (offline)
		return EXCHANGE_ACCOUNT_FOLDER_OK;

	hrefs = exchange_hierarchy_somedav_get_hrefs (hsd);
	if (!hrefs)
		return EXCHANGE_ACCOUNT_FOLDER_OK;
	if (!hrefs->len) {
		g_ptr_array_free (hrefs, TRUE);
		return EXCHANGE_ACCOUNT_FOLDER_DOES_NOT_EXIST;
	}

	iter = e_folder_exchange_bpropfind_start (hier->toplevel, NULL,
						  (const char **)hrefs->pdata,
						  hrefs->len,
						  folder_props,
						  n_folder_props);

	while ((result = e2k_result_iter_next (iter))) {
		folders_returned++;

		/* If you have "folder visible" permission but nothing
		 * else, you'll be able to fetch properties, but not
		 * see anything in the folder. In that case, PR_ACCESS
		 * will be 0, and we ignore the folder.
		 */
		if (!E2K_HTTP_STATUS_IS_SUCCESSFUL (result->status) ||
		    folder_is_unreadable (result->props)) {
			exchange_hierarchy_somedav_href_unreadable (hsd, result->href);
			continue;
		}

		folders_added++;
		iter_folder = exchange_hierarchy_webdav_parse_folder (
			EXCHANGE_HIERARCHY_WEBDAV (hier),
			hier->toplevel, result);
		exchange_hierarchy_new_folder (hier, iter_folder);
		g_object_unref (iter_folder);
	}
	status = e2k_result_iter_free (iter);

	if (folders_returned == 0)
		folder_result = EXCHANGE_ACCOUNT_FOLDER_DOES_NOT_EXIST;
	else if (folders_added == 0)
		folder_result = EXCHANGE_ACCOUNT_FOLDER_PERMISSION_DENIED;
	else
		folder_result = exchange_hierarchy_webdav_status_to_folder_result (status);

	for (i = 0; i < hrefs->len; i++)
		g_free (hrefs->pdata[i]);
	g_ptr_array_free (hrefs, TRUE);

	return folder_result;
}


GPtrArray *
exchange_hierarchy_somedav_get_hrefs (ExchangeHierarchySomeDAV *hsd)
{
	g_return_val_if_fail (EXCHANGE_IS_HIERARCHY_SOMEDAV (hsd), NULL);

	return EXCHANGE_GET_HIERARCHY_SOMEDAV_CLASS (hsd)->get_hrefs (hsd);
}

void
exchange_hierarchy_somedav_href_unreadable (ExchangeHierarchySomeDAV *hsd,
					    const char *href)
{
	g_return_if_fail (EXCHANGE_IS_HIERARCHY_SOMEDAV (hsd));
	g_return_if_fail (href != NULL);

	g_signal_emit (hsd, signals[HREF_UNREADABLE], 0, href);
}

ExchangeAccountFolderResult
exchange_hierarchy_somedav_add_folder (ExchangeHierarchySomeDAV *hsd,
				       const char *uri)
{
	ExchangeHierarchyWebDAV *hwd;
	ExchangeHierarchy *hier;
	E2kContext *ctx;
	E2kHTTPStatus status;
	E2kResult *results;
	int nresults;
	EFolder *folder;

	g_return_val_if_fail (EXCHANGE_IS_HIERARCHY_SOMEDAV (hsd),
				EXCHANGE_ACCOUNT_FOLDER_GENERIC_ERROR);
	g_return_val_if_fail (uri != NULL,
				EXCHANGE_ACCOUNT_FOLDER_GENERIC_ERROR);
	 
	hwd = EXCHANGE_HIERARCHY_WEBDAV (hsd);
	hier = EXCHANGE_HIERARCHY (hsd);
	ctx = exchange_account_get_context (hier->account);

	status = e2k_context_propfind (ctx, NULL, uri,
				       folder_props, n_folder_props,
				       &results, &nresults);
	if (!E2K_HTTP_STATUS_IS_SUCCESSFUL (status)) 
		return exchange_hierarchy_webdav_status_to_folder_result (status);
	if (nresults == 0)
		return EXCHANGE_ACCOUNT_FOLDER_DOES_NOT_EXIST;

	if (folder_is_unreadable (results[0].props))
		return EXCHANGE_ACCOUNT_FOLDER_PERMISSION_DENIED;

	folder = exchange_hierarchy_webdav_parse_folder (hwd, hier->toplevel,
							 &results[0]);
	e2k_results_free (results, nresults);

	if (!folder)
		return EXCHANGE_ACCOUNT_FOLDER_DOES_NOT_EXIST;

	exchange_hierarchy_new_folder (hier, folder);
	g_object_unref (folder);
	return EXCHANGE_ACCOUNT_FOLDER_OK;
}
