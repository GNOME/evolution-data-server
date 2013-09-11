/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * Authors: Michael Zucchi <notzed@ximian.com>
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

#if !defined (__CAMEL_H_INSIDE__) && !defined (CAMEL_COMPILATION)
#error "Only <camel/camel.h> can be included directly."
#endif

#ifndef CAMEL_IMAPX_STORE_SUMMARY_H
#define CAMEL_IMAPX_STORE_SUMMARY_H

#include <camel/camel-store-summary.h>
#include <camel/camel-imapx-mailbox.h>

/* Standard GObject macros */
#define CAMEL_TYPE_IMAPX_STORE_SUMMARY \
	(camel_imapx_store_summary_get_type ())
#define CAMEL_IMAPX_STORE_SUMMARY(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_IMAPX_STORE_SUMMARY, CamelIMAPXStoreSummary))
#define CAMEL_IMAPX_STORE_SUMMARY_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_IMAPX_STORE_SUMMARY, CamelIMAPXStoreSummaryClass))
#define CAMEL_IS_IMAPX_STORE_SUMMARY(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_IMAPX_STORE_SUMMARY))
#define CAMEL_IS_IMAPX_STORE_SUMMARY_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_IMAPX_STORE_SUMMARY))
#define CAMEL_IMAPX_STORE_SUMMARY_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_IMAPX_STORE_SUMMARY, CamelIMAPXStoreSummaryClass))

G_BEGIN_DECLS

typedef struct _CamelIMAPXStoreSummary CamelIMAPXStoreSummary;
typedef struct _CamelIMAPXStoreSummaryClass CamelIMAPXStoreSummaryClass;

typedef struct _CamelIMAPXStoreInfo CamelIMAPXStoreInfo;

struct _CamelIMAPXStoreInfo {
	CamelStoreInfo info;
	gchar *mailbox_name;
	gchar separator;
};

typedef struct _CamelIMAPXStoreNamespace CamelIMAPXStoreNamespace;

struct _CamelIMAPXStoreNamespace {
	CamelIMAPXStoreNamespace *next;
	gchar *prefix;		/* mailbox prefix, no trailing separator */
	gchar sep;		/* mailbox separator */
};

typedef struct _CamelIMAPXNamespaceList {
	CamelIMAPXStoreNamespace *personal;
	CamelIMAPXStoreNamespace *other;
	CamelIMAPXStoreNamespace *shared;
} CamelIMAPXNamespaceList;

struct _CamelIMAPXStoreSummary {
	CamelStoreSummary parent;

	/* header info */
	guint32 version;	/* version of base part of file */
	CamelIMAPXNamespaceList *namespaces; /* eventually to be a list */
};

struct _CamelIMAPXStoreSummaryClass {
	CamelStoreSummaryClass parent_class;
};

GType		camel_imapx_store_summary_get_type (void);
CamelIMAPXStoreSummary *
		camel_imapx_store_summary_new	(void);

/* TODO: this api needs some more work, needs to support lists */
CamelIMAPXStoreNamespace *
		camel_imapx_store_summary_namespace_find_by_path
						(CamelIMAPXStoreSummary *s,
						 const gchar *path);
CamelIMAPXStoreNamespace *
		camel_imapx_store_summary_namespace_find_by_mailbox
						(CamelIMAPXStoreSummary *s,
						 const gchar *mailbox);

CamelIMAPXStoreInfo *
		camel_imapx_store_summary_mailbox
						(CamelIMAPXStoreSummary *s,
						 const gchar *mailbox_name);
CamelIMAPXStoreInfo *
		camel_imapx_store_summary_add_from_mailbox
						(CamelIMAPXStoreSummary *s,
						 CamelIMAPXMailbox *mailbox);

G_END_DECLS

#endif /* CAMEL_IMAP_STORE_SUMMARY_H */
