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

#ifndef CAMEL_IMAP_SUMMARY_H
#define CAMEL_IMAP_SUMMARY_H

#include <camel/camel.h>

/* Standard GObject macros */
#define CAMEL_TYPE_IMAP_SUMMARY \
	(camel_imap_summary_get_type ())
#define CAMEL_IMAP_SUMMARY(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_IMAP_SUMMARY, CamelImapSummary))
#define CAMEL_IMAP_SUMMARY_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_IMAP_SUMMARY, CamelImapSummaryClass))
#define CAMEL_IS_IMAP_SUMMARY(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_IMAP_SUMMARY))
#define CAMEL_IS_IMAP_SUMMARY_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_IMAP_SUMMARY))
#define CAMEL_IMAP_SUMMARY_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_IMAP_SUMMARY, CamelImapSummaryClass))

#define CAMEL_IMAP_SERVER_FLAGS (CAMEL_MESSAGE_ANSWERED | \
				 CAMEL_MESSAGE_DELETED | \
				 CAMEL_MESSAGE_DRAFT | \
				 CAMEL_MESSAGE_FLAGGED | \
				 CAMEL_MESSAGE_SEEN)

G_BEGIN_DECLS

enum {
	CAMEL_IMAP_MESSAGE_RECENT = 1<<17
};

typedef struct _CamelImapSummary CamelImapSummary;
typedef struct _CamelImapSummaryClass CamelImapSummaryClass;

typedef struct _CamelImapMessageContentInfo {
	CamelMessageContentInfo info;

} CamelImapMessageContentInfo;

typedef struct _CamelImapMessageInfo {
	CamelMessageInfoBase info;

	guint32 server_flags;
} CamelImapMessageInfo;

struct _CamelImapSummary {
	CamelFolderSummary parent;

	guint32 version;
	guint32 validity;
};

struct _CamelImapSummaryClass {
	CamelFolderSummaryClass parent_class;

};

GType               camel_imap_summary_get_type     (void);
CamelFolderSummary *camel_imap_summary_new          (struct _CamelFolder *folder, const gchar *filename);

void camel_imap_summary_add_offline (CamelFolderSummary *summary,
				     const gchar *uid,
				     CamelMimeMessage *message,
				     const CamelMessageInfo *info);

void camel_imap_summary_add_offline_uncached (CamelFolderSummary *summary,
					      const gchar *uid,
					      const CamelMessageInfo *info);

G_END_DECLS

#endif /* CAMEL_IMAP_SUMMARY_H */
