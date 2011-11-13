/*
 *  Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 *  Authors: Michael Zucchi <notzed@ximian.com>
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

#ifndef CAMEL_NNTP_SUMMARY_H
#define CAMEL_NNTP_SUMMARY_H

#include <camel/camel.h>

/* Standard GObject macros */
#define CAMEL_TYPE_NNTP_SUMMARY \
	(camel_nntp_summary_get_type ())
#define CAMEL_NNTP_SUMMARY(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_NNTP_SUMMARY, CamelNNTPSummary))
#define CAMEL_NNTP_SUMMARY_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_NNTP_SUMMARY, CamelNNTPSummaryClass))
#define CAMEL_IS_NNTP_SUMMARY(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_NNTP_SUMMARY))
#define CAMEL_IS_NNTP_SUMMARY_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_NNTP_SUMMARY))
#define CAMEL_NNTP_SUMMARY_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_NNTP_SUMMARY, CamelNNTPSummaryClass))

G_BEGIN_DECLS

struct _CamelNNTPStore;
struct _CamelFolderChangeInfo;

typedef struct _CamelNNTPSummary CamelNNTPSummary;
typedef struct _CamelNNTPSummaryClass CamelNNTPSummaryClass;
typedef struct _CamelNNTPSummaryPrivate CamelNNTPSummaryPrivate;

struct _CamelNNTPSummary {
	CamelFolderSummary parent;
	CamelNNTPSummaryPrivate *priv;

	guint32 version;
	guint32 high, low;
};

struct _CamelNNTPSummaryClass {
	CamelFolderSummaryClass parent_class;
};

GType		camel_nntp_summary_get_type	(void);
CamelNNTPSummary *
		camel_nntp_summary_new		(CamelFolder *folder);
gint		camel_nntp_summary_check	(CamelNNTPSummary *cns,
						 struct _CamelNNTPStore *store,
						 gchar *line,
						 CamelFolderChangeInfo *changes,
						 GCancellable *cancellable,
						 GError **error);

G_END_DECLS

#endif /* CAMEL_NNTP_SUMMARY_H */

