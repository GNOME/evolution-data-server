/*
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * Authors: Not Zed <notzed@lostzed.mmc.com.au>
 *
 * This library is free software you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef CAMEL_MAILDIR_SUMMARY_H
#define CAMEL_MAILDIR_SUMMARY_H

#include "camel-local-summary.h"

/* Standard GObject macros */
#define CAMEL_TYPE_MAILDIR_SUMMARY \
	(camel_maildir_summary_get_type ())
#define CAMEL_MAILDIR_SUMMARY(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_MAILDIR_SUMMARY, CamelMaildirSummary))
#define CAMEL_MAILDIR_SUMMARY_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_MAILDIR_SUMMARY, CamelMaildirSummaryClass))
#define CAMEL_IS_MAILDIR_SUMMARY(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_MAILDIR_SUMMARY))
#define CAMEL_IS_MAILDIR_SUMMARY_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_MAILDIR_SUMMARY))
#define CAMEL_MAILDIR_SUMMARY_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_MAILDIR_SUMMARY, CamelMaildirSummaryClass))

G_BEGIN_DECLS

typedef struct _CamelMaildirSummary CamelMaildirSummary;
typedef struct _CamelMaildirSummaryClass CamelMaildirSummaryClass;
typedef struct _CamelMaildirSummaryPrivate CamelMaildirSummaryPrivate;

typedef struct _CamelMaildirMessageInfo {
	CamelLocalMessageInfo info;

	gchar *filename;		/* maildir has this annoying status on the end of the filename, use this to get the real message id */
} CamelMaildirMessageInfo;

struct _CamelMaildirSummary {
	CamelLocalSummary parent;
	CamelMaildirSummaryPrivate *priv;
};

struct _CamelMaildirSummaryClass {
	CamelLocalSummaryClass parent_class;
};

GType	 camel_maildir_summary_get_type	(void);
CamelMaildirSummary	*camel_maildir_summary_new	(struct _CamelFolder *folder, const gchar *maildirdir, CamelIndex *index);

/* convert some info->flags to/from the messageinfo */
gchar *camel_maildir_summary_info_to_name (const CamelMaildirMessageInfo *info);
gint camel_maildir_summary_name_to_info (CamelMaildirMessageInfo *info, const gchar *name);

/* TODO: could proably use get_string stuff */
#define camel_maildir_info_filename(x) (((CamelMaildirMessageInfo *)x)->filename)
#define camel_maildir_info_set_filename(x, s) (g_free(((CamelMaildirMessageInfo *)x)->filename),((CamelMaildirMessageInfo *)x)->filename = s)

G_END_DECLS

#endif /* CAMEL_MAILDIR_SUMMARY_H */
