/*
 *  Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 *  Authors:
 *	parthasarathi susarla <sparthasarathi@novell.com>
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

#ifndef CAMEL_GW_SUMMARY_H
#define CAMEL_GW_SUMMARY_H

#include <camel/camel.h>

/* Standard GObject macros */
#define CAMEL_TYPE_GROUPWISE_SUMMARY \
	(camel_groupwise_summary_get_type ())
#define CAMEL_GROUPWISE_SUMMARY(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_GROUPWISE_SUMMARY, CamelGroupwiseSummary))
#define CAMEL_GROUPWISE_SUMMARY_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_GROUPWISE_SUMMARY, CamelGroupwiseSummaryClass))
#define CAMEL_IS_GROUPWISE_SUMMARY(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_GROUPWISE_SUMMARY))
#define CAMEL_IS_GROUPWISE_SUMMARY_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_GROUPWISE_SUMMARY))
#define CAMEL_GROUPWISE_SUMMARY_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_GROUPWISE_SUMMARY, CamelGroupwiseSummaryClass))

G_BEGIN_DECLS

typedef struct _CamelGroupwiseSummary CamelGroupwiseSummary;
typedef struct _CamelGroupwiseSummaryClass CamelGroupwiseSummaryClass;
typedef struct _CamelGroupwiseMessageInfo CamelGroupwiseMessageInfo;
typedef struct _CamelGroupwiseMessageContentInfo CamelGroupwiseMessageContentInfo;

/* extra summary flags*/
enum {
	CAMEL_GW_MESSAGE_JUNK = 1<<17,
	CAMEL_GW_MESSAGE_NOJUNK = 1<<18
};

struct _CamelGroupwiseMessageInfo {
	CamelMessageInfoBase info;

	guint32 server_flags;
} ;

struct _CamelGroupwiseMessageContentInfo {
	CamelMessageContentInfo info;
} ;

struct _CamelGroupwiseSummary {
	CamelFolderSummary parent;

	gchar *time_string;
	gint32 version;
	gint32 validity;
} ;

struct _CamelGroupwiseSummaryClass {
	CamelFolderSummaryClass parent_class;
} ;

GType camel_groupwise_summary_get_type (void);

CamelFolderSummary *camel_groupwise_summary_new (struct _CamelFolder *folder, const gchar *filename);

void camel_gw_summary_add_offline (CamelFolderSummary *summary, const gchar *uid, CamelMimeMessage *messgae, const CamelMessageInfo *info);

void camel_gw_summary_add_offline_uncached (CamelFolderSummary *summary, const gchar *uid, const CamelMessageInfo *info);
void groupwise_summary_clear (CamelFolderSummary *summary, gboolean uncache);

G_END_DECLS

#endif /* CAMEL_GW_SUMMARY_H */
