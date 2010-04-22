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

/* currently, this is just a straigt s/imap/nntp from the IMAP file*/

#ifndef CAMEL_NNTP_STORE_SUMMARY_H
#define CAMEL_NNTP_STORE_SUMMARY_H

#include <camel/camel.h>

/* Standard GObject macros */
#define CAMEL_TYPE_NNTP_STORE_SUMMARY \
	(camel_nntp_store_summary_get_type ())
#define CAMEL_NNTP_STORE_SUMMARY(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_NNTP_STORE_SUMMARY, CamelNNTPStoreSummary))
#define CAMEL_NNTP_STORE_SUMMARY_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_NNTP_STORE_SUMMARY, CamelNNTPStoreSummaryClass))
#define CAMEL_IS_NNTP_STORE_SUMMARY(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_NNTP_STORE_SUMMARY))
#define CAMEL_IS_NNTP_STORE_SUMMARY_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_NNTP_STORE_SUMMARY))
#define CAMEL_NNTP_STORE_SUMMARY_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_NNTP_STORE_SUMMARY, CamelNNTPStoreSummaryClass))

G_BEGIN_DECLS

typedef struct _CamelNNTPStoreSummary CamelNNTPStoreSummary;
typedef struct _CamelNNTPStoreSummaryClass CamelNNTPStoreSummaryClass;
typedef struct _CamelNNTPStoreSummaryPrivate CamelNNTPStoreSummaryPrivate;

typedef struct _CamelNNTPStoreInfo CamelNNTPStoreInfo;

enum {
	CAMEL_NNTP_STORE_INFO_FULL_NAME = CAMEL_STORE_INFO_LAST,
	CAMEL_NNTP_STORE_INFO_LAST
};

struct _CamelNNTPStoreInfo {
	CamelStoreInfo info;
	gchar *full_name;
	guint32 first;		/* from LIST or NEWGROUPS return */
	guint32 last;
};

#define NNTP_DATE_SIZE 14

struct _CamelNNTPStoreSummary {
	CamelStoreSummary summary;
	CamelNNTPStoreSummaryPrivate *priv;

	/* header info */
	guint32 version;	/* version of base part of file */
	gchar last_newslist[NNTP_DATE_SIZE];
};

struct _CamelNNTPStoreSummaryClass {
	CamelStoreSummaryClass summary_class;
};

GType			 camel_nntp_store_summary_get_type	(void);
CamelNNTPStoreSummary      *camel_nntp_store_summary_new	(void);

/* TODO: this api needs some more work, needs to support lists */
/*CamelNNTPStoreNamespace *camel_nntp_store_summary_namespace_new(CamelNNTPStoreSummary *s, const gchar *full_name, gchar dir_sep);*/
/*void camel_nntp_store_summary_namespace_set(CamelNNTPStoreSummary *s, CamelNNTPStoreNamespace *ns);*/
/*CamelNNTPStoreNamespace *camel_nntp_store_summary_namespace_find_path(CamelNNTPStoreSummary *s, const gchar *path);*/
/*CamelNNTPStoreNamespace *camel_nntp_store_summary_namespace_find_full(CamelNNTPStoreSummary *s, const gchar *full_name);*/

/* helper macro's */
#define camel_nntp_store_info_full_name(s, i) (camel_store_info_string((CamelStoreSummary *)s, (const CamelStoreInfo *)i, CAMEL_NNTP_STORE_INFO_FULL_NAME))

/* converts to/from utf8 canonical nasmes */
gchar *camel_nntp_store_summary_full_to_path(CamelNNTPStoreSummary *s, const gchar *full_name, gchar dir_sep);

gchar *camel_nntp_store_summary_path_to_full(CamelNNTPStoreSummary *s, const gchar *path, gchar dir_sep);
gchar *camel_nntp_store_summary_dotted_to_full(CamelNNTPStoreSummary *s, const gchar *dotted, gchar dir_sep);

CamelNNTPStoreInfo *camel_nntp_store_summary_full_name(CamelNNTPStoreSummary *s, const gchar *full_name);
CamelNNTPStoreInfo *camel_nntp_store_summary_add_from_full(CamelNNTPStoreSummary *s, const gchar *full_name, gchar dir_sep);

/* a convenience lookup function. always use this if path known */
gchar *camel_nntp_store_summary_full_from_path(CamelNNTPStoreSummary *s, const gchar *path);

G_END_DECLS

#endif /* CAMEL_NNTP_STORE_SUMMARY_H */
