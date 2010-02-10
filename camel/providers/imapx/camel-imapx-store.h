/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-imap-store.h : class for an imap store */

/*
 * Authors: Michael Zucchi <notzed@ximian.com>
 *
 * Copyright (C) 2002 Ximian, Inc. (www.ximian.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */

#ifndef CAMEL_IMAPX_STORE_H
#define CAMEL_IMAPX_STORE_H 1

#ifdef __cplusplus
extern "C" {
#pragma }
#endif /* __cplusplus }*/

#include <camel/camel-types.h>
#include <camel/camel-store.h>
#include "camel-imapx-store-summary.h"
#include <camel/camel-offline-store.h>

#define CAMEL_IMAPX_STORE_TYPE     (camel_imapx_store_get_type ())
#define CAMEL_IMAPX_STORE(obj)     (CAMEL_CHECK_CAST((obj), CAMEL_IMAPX_STORE_TYPE, CamelIMAPXStore))
#define CAMEL_IMAPX_STORE_CLASS(k) (CAMEL_CHECK_CLASS_CAST ((k), CAMEL_IMAPX_STORE_TYPE, CamelIMAPXStoreClass))
#define CAMEL_IS_IMAP_STORE(o)    (CAMEL_CHECK_TYPE((o), CAMEL_IMAPX_STORE_TYPE))

#define IMAPX_OVERRIDE_NAMESPACE	(1 << 0)
#define IMAPX_CHECK_ALL			(1 << 1)
#define IMAPX_FILTER_INBOX		(1 << 2)
#define IMAPX_FILTER_JUNK		(1 << 3)
#define IMAPX_FILTER_JUNK_INBOX		(1 << 4)
#define IMAPX_SUBSCRIPTIONS		(1 << 5)
#define IMAPX_CHECK_LSUB		(1 << 6)
#define IMAPX_USE_IDLE			(1 << 7)

typedef struct {
	CamelOfflineStore parent_object;

	struct _CamelIMAPXServer *server;

	CamelIMAPXStoreSummary *summary; /* in-memory list of folders */
	gchar *namespace, dir_sep, *base_url, *storage_path;

	guint32 rec_options;
	
	/* if we had a login error, what to show to user */
	gchar *login_error;

	GPtrArray *pending_list;
} CamelIMAPXStore;

typedef struct {
	CamelOfflineStoreClass parent_class;

} CamelIMAPXStoreClass;

/* Standard Camel function */
CamelType camel_imapx_store_get_type (void);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* CAMEL_IMAPX_STORE_H */

