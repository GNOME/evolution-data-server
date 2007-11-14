/*
 *  Copyright (C) 2000 Ximian Inc.
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

#ifndef _CAMEL_IMAP_SUMMARY_H
#define _CAMEL_IMAP_SUMMARY_H

#include "camel-imap-types.h"
#include <camel/camel-folder-summary.h>
#include <camel/camel-exception.h>

#define CAMEL_IMAP_SUMMARY(obj)         CAMEL_CHECK_CAST (obj, camel_imap_summary_get_type (), CamelImapSummary)
#define CAMEL_IMAP_SUMMARY_CLASS(klass) CAMEL_CHECK_CLASS_CAST (klass, camel_imap_summary_get_type (), CamelImapSummaryClass)
#define CAMEL_IS_IMAP_SUMMARY(obj)      CAMEL_CHECK_TYPE (obj, camel_imap_summary_get_type ())

#define CAMEL_IMAP_SERVER_FLAGS (CAMEL_MESSAGE_ANSWERED | \
				 CAMEL_MESSAGE_DELETED | \
				 CAMEL_MESSAGE_DRAFT | \
				 CAMEL_MESSAGE_FLAGGED | \
				 CAMEL_MESSAGE_SEEN)

G_BEGIN_DECLS

enum {
	CAMEL_IMAP_MESSAGE_RECENT = 1<<17,

	/* We store the label in flags too, to ease processing.
	   TODO: Move this to camel-folder-summary, BUT don't use
	   1 bit per label, instead use a bitfield of 3 bits */

	CAMEL_IMAP_MESSAGE_LABEL1 = 1<<18,
	CAMEL_IMAP_MESSAGE_LABEL2 = 1<<19,
	CAMEL_IMAP_MESSAGE_LABEL3 = 1<<20,
	CAMEL_IMAP_MESSAGE_LABEL4 = 1<<21,
	CAMEL_IMAP_MESSAGE_LABEL5 = 1<<22,
	CAMEL_IMAP_MESSAGE_LABEL_MASK = 31<<18
};

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

CamelType               camel_imap_summary_get_type     (void);
CamelFolderSummary *camel_imap_summary_new          (struct _CamelFolder *folder, const char *filename);

void camel_imap_summary_add_offline (CamelFolderSummary *summary,
				     const char *uid,
				     CamelMimeMessage *message,
				     const CamelMessageInfo *info);

void camel_imap_summary_add_offline_uncached (CamelFolderSummary *summary,
					      const char *uid,
					      const CamelMessageInfo *info);

G_END_DECLS

#endif /* ! _CAMEL_IMAP_SUMMARY_H */
