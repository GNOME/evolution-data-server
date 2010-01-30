/*
 *  Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 *  Authors:
 *    Michael Zucchi <notzed@ximian.com>
 *    Dan Winship <danw@ximian.com>
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

#ifndef _CAMEL_VEE_SUMMARY_H
#define _CAMEL_VEE_SUMMARY_H

#include <camel/camel-folder-summary.h>
#include <camel/camel-exception.h>

#define CAMEL_VEE_SUMMARY(obj)         CAMEL_CHECK_CAST (obj, camel_vee_summary_get_type (), CamelVeeSummary)
#define CAMEL_VEE_SUMMARY_CLASS(klass) CAMEL_CHECK_CLASS_CAST (klass, camel_vee_summary_get_type (), CamelVeeSummaryClass)
#define CAMEL_IS_VEE_SUMMARY(obj)      CAMEL_CHECK_TYPE (obj, camel_vee_summary_get_type ())

G_BEGIN_DECLS

struct _CamelVeeFolder;
struct _CamelFolder;

typedef struct _CamelVeeSummary CamelVeeSummary;
typedef struct _CamelVeeSummaryClass CamelVeeSummaryClass;

typedef struct _CamelVeeMessageInfo CamelVeeMessageInfo;

struct _CamelVeeMessageInfo {
	CamelMessageInfo info;
	CamelFolderSummary *summary;
	guint32 old_flags;  /* These are just for identifying changed flags */
};

struct _CamelVeeSummary {
	CamelFolderSummary summary;
	gboolean force_counts;
	guint32 fake_visible_count;
};

struct _CamelVeeSummaryClass {
	CamelFolderSummaryClass parent_class;

};

CamelType               camel_vee_summary_get_type     (void);
CamelFolderSummary *camel_vee_summary_new(struct _CamelFolder *parent);

CamelVeeMessageInfo * camel_vee_summary_add(CamelVeeSummary *s, CamelFolderSummary *summary, const gchar *uid, const gchar hash[8]);
GPtrArray * camel_vee_summary_get_ids (CamelVeeSummary *summary, gchar hash[8]);
void camel_vee_summary_load_check_unread_vfolder  (CamelVeeSummary *vs);

G_END_DECLS

#endif /* _CAMEL_VEE_SUMMARY_H */

