/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * Authors: Parthasarathi Susarla <sparthasrathi@novell.com>
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

#ifndef _CAMEL_GW_STORE_SUMMARY_H
#define _CAMEL_GW_STORE_SUMMARY_H

#include <camel/camel-object.h>
#include <camel/camel-store-summary.h>

#define CAMEL_GW_STORE_SUMMARY(obj)         CAMEL_CHECK_CAST (obj, camel_groupwise_store_summary_get_type (), CamelGroupwiseStoreSummary)
#define CAMEL_GW_STORE_SUMMARY_CLASS(klass) CAMEL_CHECK_CLASS_CAST (klass, camel_groupwise_store_summary_get_type (), CamelGroupwiseStoreSummaryClass)
#define CAMEL_IS_GW_STORE_SUMMARY(obj)      CAMEL_CHECK_TYPE (obj, camel_groupwise_store_summary_get_type ())

G_BEGIN_DECLS

typedef struct _CamelGroupwiseStoreSummary      CamelGroupwiseStoreSummary;
typedef struct _CamelGroupwiseStoreSummaryClass CamelGroupwiseStoreSummaryClass;

typedef struct _CamelGroupwiseStoreInfo CamelGroupwiseStoreInfo;

enum {
	CAMEL_GW_STORE_INFO_FULL_NAME = CAMEL_STORE_INFO_LAST,
	CAMEL_GW_STORE_INFO_LAST
};

struct _CamelGroupwiseStoreInfo {
	CamelStoreInfo info;
	gchar *full_name;
};

typedef struct _CamelGroupwiseStoreNamespace CamelGroupwiseStoreNamespace;

struct _CamelGroupwiseStoreNamespace {
	gchar *path;             /* display path */
	gchar *full_name;        /* real name */
	gchar sep;               /* directory separator */
};

struct _CamelGroupwiseStoreSummary {
	CamelStoreSummary summary;

	struct _CamelGroupwiseStoreSummaryPrivate *priv;

	/* header info */
	guint32 version;        /* version of base part of file */
	guint32 capabilities;
	CamelGroupwiseStoreNamespace *namespace; /* eventually to be a list */
};

struct _CamelGroupwiseStoreSummaryClass {
	CamelStoreSummaryClass summary_class;
};

CamelType                        camel_groupwise_store_summary_get_type      (void);
CamelGroupwiseStoreSummary      *camel_groupwise_store_summary_new        (void);
CamelGroupwiseStoreInfo *camel_groupwise_store_summary_full_name(CamelGroupwiseStoreSummary *s, const gchar *full_name);
CamelGroupwiseStoreInfo *camel_groupwise_store_summary_add_from_full(CamelGroupwiseStoreSummary *s, const gchar *full, gchar dir_sep);

gchar *camel_groupwise_store_summary_full_to_path(CamelGroupwiseStoreSummary *s, const gchar *full_name, gchar dir_sep);
gchar *camel_groupwise_store_summary_path_to_full(CamelGroupwiseStoreSummary *s, const gchar *path, gchar dir_sep);
gchar *camel_groupwise_store_summary_full_from_path(CamelGroupwiseStoreSummary *s, const gchar *path);

CamelGroupwiseStoreNamespace *camel_groupwise_store_summary_namespace_new(CamelGroupwiseStoreSummary *s, const gchar *full_name, gchar dir_sep);
CamelGroupwiseStoreNamespace *camel_groupwise_store_summary_namespace_find_path(CamelGroupwiseStoreSummary *s, const gchar *path);
void camel_groupwise_store_summary_namespace_set(CamelGroupwiseStoreSummary *s, CamelGroupwiseStoreNamespace *ns);

#define camel_groupwise_store_info_full_name(s, i) (camel_store_info_string((CamelStoreSummary *)s, (const CamelStoreInfo *)i, CAMEL_STORE_INFO_LAST))

G_END_DECLS

#endif /* _CAMEL_IMAP_STORE_SUMMARY_H */
