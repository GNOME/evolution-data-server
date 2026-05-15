/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 * SPDX-FileContributor: Michael Zucchi <notzed@ximian.com>
 * SPDX-FileContributor: Dan Winship <danw@ximian.com>
 */

#ifndef CAMEL_IMAPX_SUMMARY_H
#define CAMEL_IMAPX_SUMMARY_H

#include <camel/camel.h>

/* Standard GObject macros */
#define CAMEL_TYPE_IMAPX_SUMMARY \
	(camel_imapx_summary_get_type ())
#define CAMEL_IMAPX_SUMMARY(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_IMAPX_SUMMARY, CamelIMAPXSummary))
#define CAMEL_IMAPX_SUMMARY_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_IMAPX_SUMMARY, CamelIMAPXSummaryClass))
#define CAMEL_IS_IMAPX_SUMMARY(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_IMAPX_SUMMARY))
#define CAMEL_IS_IMAPX_SUMMARY_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_IMAPX_SUMMARY))
#define CAMEL_IMAPX_SUMMARY_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_IMAPX_SUMMARY, CamelIMAPXSummaryClass))

#define CAMEL_IMAPX_SERVER_FLAGS \
	(CAMEL_MESSAGE_ANSWERED | \
	 CAMEL_MESSAGE_DELETED | \
	 CAMEL_MESSAGE_DRAFT | \
	 CAMEL_MESSAGE_FLAGGED | \
	 CAMEL_MESSAGE_JUNK | \
	 CAMEL_MESSAGE_NOTJUNK | \
	 CAMEL_MESSAGE_SEEN)

G_BEGIN_DECLS

typedef struct _CamelIMAPXSummary CamelIMAPXSummary;
typedef struct _CamelIMAPXSummaryClass CamelIMAPXSummaryClass;

struct _CamelIMAPXSummary {
	CamelFolderSummary parent;

	guint32 version;
	guint32 uidnext;
	guint64 validity;
	guint64 modseq;
};

struct _CamelIMAPXSummaryClass {
	CamelFolderSummaryClass parent_class;

	/* Padding for future expansion */
	gpointer reserved[20];
};

GType		camel_imapx_summary_get_type	(void);
CamelFolderSummary *
		camel_imapx_summary_new		(CamelFolder *folder);

G_END_DECLS

#endif /* CAMEL_IMAPX_SUMMARY_H */
