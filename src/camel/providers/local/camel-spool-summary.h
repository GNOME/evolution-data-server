/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 * SPDX-FileContributor: Michael Zucchi <notzed@ximian.com>
 */

#ifndef CAMEL_SPOOL_SUMMARY_H
#define CAMEL_SPOOL_SUMMARY_H

#include <camel/camel.h>

#include "camel-mbox-summary.h"

/* Standard GObject macros */
#define CAMEL_TYPE_SPOOL_SUMMARY \
	(camel_spool_summary_get_type ())
#define CAMEL_SPOOL_SUMMARY(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_SPOOL_SUMMARY, CamelSpoolSummary))
#define CAMEL_SPOOL_SUMMARY_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_SPOOL_SUMMARY, CamelSpoolSummaryClass))
#define CAMEL_IS_SPOOL_SUMMARY(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_SPOOL_SUMMARY))
#define CAMEL_IS_SPOOL_SUMMARY_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_SPOOL_SUMMARY))
#define CAMEL_SPOOL_SUMMARY_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_SPOOL_SUMMARY, CamelSpoolSummaryClass))

G_BEGIN_DECLS

typedef struct _CamelSpoolSummary CamelSpoolSummary;
typedef struct _CamelSpoolSummaryClass CamelSpoolSummaryClass;

struct _CamelSpoolSummary {
	CamelMboxSummary parent;
};

struct _CamelSpoolSummaryClass {
	CamelMboxSummaryClass parent_class;

	/* Padding for future expansion */
	gpointer reserved[20];
};

GType	camel_spool_summary_get_type	(void);

/* create the summary, in-memory only */
CamelSpoolSummary *camel_spool_summary_new (struct _CamelFolder *, const gchar *filename);

G_END_DECLS

#endif /* CAMEL_SPOOL_SUMMARY_H */
