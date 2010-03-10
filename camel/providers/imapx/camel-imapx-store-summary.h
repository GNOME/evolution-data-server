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

#ifndef _CAMEL_IMAPX_STORE_SUMMARY_H
#define _CAMEL_IMAPX_STORE_SUMMARY_H

#include <camel/camel-object.h>
#include <camel/camel-store-summary.h>

#define CAMEL_IMAPX_STORE_SUMMARY(obj)         CAMEL_CHECK_CAST (obj, camel_imapx_store_summary_get_type (), CamelIMAPXStoreSummary)
#define CAMEL_IMAPX_STORE_SUMMARY_CLASS(klass) CAMEL_CHECK_CLASS_CAST (klass, camel_imapx_store_summary_get_type (), CamelIMAPXStoreSummaryClass)
#define CAMEL_IS_IMAPX_STORE_SUMMARY(obj)      CAMEL_CHECK_TYPE (obj, camel_imapx_store_summary_get_type ())

G_BEGIN_DECLS

typedef struct _CamelIMAPXStoreSummary      CamelIMAPXStoreSummary;
typedef struct _CamelIMAPXStoreSummaryClass CamelIMAPXStoreSummaryClass;

typedef struct _CamelIMAPXStoreInfo CamelIMAPXStoreInfo;

enum {
	CAMEL_IMAPX_STORE_INFO_FULL_NAME = CAMEL_STORE_INFO_LAST,
	CAMEL_IMAPX_STORE_INFO_LAST
};

struct _CamelIMAPXStoreInfo {
	CamelStoreInfo info;
	gchar *full_name;
};

typedef struct _CamelIMAPXStoreNamespace CamelIMAPXStoreNamespace;

struct _CamelIMAPXStoreNamespace {
	struct _CamelIMAPXStoreNamespace *next;
	gchar *path;		/* display path */
	gchar *full_name;	/* real name */
	gchar sep;		/* directory separator */
};

typedef struct _CamelIMAPXNamespaceList {
	CamelIMAPXStoreNamespace *personal;
	CamelIMAPXStoreNamespace *other;
	CamelIMAPXStoreNamespace *shared;
} CamelIMAPXNamespaceList;

struct _CamelIMAPXStoreSummary {
	CamelStoreSummary summary;

	struct _CamelIMAPXStoreSummaryPrivate *priv;

	/* header info */
	guint32 version;	/* version of base part of file */
	guint32 capabilities;
	CamelIMAPXNamespaceList *namespaces; /* eventually to be a list */
};

struct _CamelIMAPXStoreSummaryClass {
	CamelStoreSummaryClass summary_class;
};

CamelType			 camel_imapx_store_summary_get_type	(void);
CamelIMAPXStoreSummary      *camel_imapx_store_summary_new	(void);

/* TODO: this api needs some more work, needs to support lists */
CamelIMAPXStoreNamespace *camel_imapx_store_summary_namespace_new(CamelIMAPXStoreSummary *s, const gchar *full_name, gchar dir_sep);
void camel_imapx_store_summary_namespace_set(CamelIMAPXStoreSummary *s, CamelIMAPXStoreNamespace *ns);
CamelIMAPXStoreNamespace *camel_imapx_store_summary_namespace_find_path(CamelIMAPXStoreSummary *s, const gchar *path);
CamelIMAPXStoreNamespace *camel_imapx_store_summary_namespace_find_full(CamelIMAPXStoreSummary *s, const gchar *full_name);

/* converts to/from utf8 canonical nasmes */
gchar *camel_imapx_store_summary_full_to_path(CamelIMAPXStoreSummary *s, const gchar *full_name, gchar dir_sep);
gchar *camel_imapx_store_summary_path_to_full(CamelIMAPXStoreSummary *s, const gchar *path, gchar dir_sep);

CamelIMAPXStoreInfo *camel_imapx_store_summary_full_name(CamelIMAPXStoreSummary *s, const gchar *full_name);
CamelIMAPXStoreInfo *camel_imapx_store_summary_add_from_full(CamelIMAPXStoreSummary *s, const gchar *full_name, gchar dir_sep);

/* a convenience lookup function. always use this if path known */
gchar *camel_imapx_store_summary_full_from_path(CamelIMAPXStoreSummary *s, const gchar *path);

void camel_imapx_store_summary_set_namespaces (CamelIMAPXStoreSummary *summary, const CamelIMAPXNamespaceList *nsl);

/* helpe macro's */
#define camel_imapx_store_info_full_name(s, i) (camel_store_info_string((CamelStoreSummary *)s, (const CamelStoreInfo *)i, CAMEL_IMAPX_STORE_INFO_FULL_NAME))

G_END_DECLS

#endif /* _CAMEL_IMAP_STORE_SUMMARY_H */
