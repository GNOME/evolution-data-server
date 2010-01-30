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

#ifndef _CAMEL_IMAPX_SUMMARY_H
#define _CAMEL_IMAPX_SUMMARY_H

//#include "camel-imap-types.h"
#include <camel/camel-folder-summary.h>
#include <camel/camel-exception.h>

#define CAMEL_IMAPX_SUMMARY(obj)         CAMEL_CHECK_CAST (obj, camel_imapx_summary_get_type (), CamelIMAPXSummary)
#define CAMEL_IMAPX_SUMMARY_CLASS(klass) CAMEL_CHECK_CLASS_CAST (klass, camel_imapx_summary_get_type (), CamelIMAPXSummaryClass)
#define CAMEL_IS_IMAPX_SUMMARY(obj)      CAMEL_CHECK_TYPE (obj, camel_imapx_summary_get_type ())

#define CAMEL_IMAPX_SERVER_FLAGS (CAMEL_MESSAGE_ANSWERED | \
				 CAMEL_MESSAGE_DELETED | \
				 CAMEL_MESSAGE_DRAFT | \
				 CAMEL_MESSAGE_FLAGGED | \
				 CAMEL_MESSAGE_SEEN)

G_BEGIN_DECLS

typedef struct _CamelIMAPXSummaryClass CamelIMAPXSummaryClass;
typedef struct _CamelIMAPXSummary CamelIMAPXSummary;

typedef struct _CamelIMAPXMessageContentInfo {
	CamelMessageContentInfo info;

} CamelIMAPXMessageContentInfo;

typedef struct _CamelIMAPXMessageInfo {
	CamelMessageInfoBase info;

	guint32 server_flags;
	struct _CamelFlag *server_user_flags;
} CamelIMAPXMessageInfo;

struct _CamelIMAPXSummary {
	CamelFolderSummary parent;

	guint32 version;
	guint32 validity;
};

struct _CamelIMAPXSummaryClass {
	CamelFolderSummaryClass parent_class;

};

CamelType               camel_imapx_summary_get_type     (void);
CamelFolderSummary *camel_imapx_summary_new          (struct _CamelFolder *folder, const gchar *filename);

void camel_imapx_summary_add_offline (CamelFolderSummary *summary,
				     const gchar *uid,
				     CamelMimeMessage *message,
				     const CamelMessageInfo *info);

void camel_imapx_summary_add_offline_uncached (CamelFolderSummary *summary,
					      const gchar *uid,
					      const CamelMessageInfo *info);

G_END_DECLS

#endif /* _CAMEL_IMAPX_SUMMARY_H */
