/*
 *  Authors: Michael Zucchi <notzed@ximian.com>
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef _CAMEL_IMAPX_VIEW_SUMMARY_H
#define _CAMEL_IMAPX_VIEW_SUMMARY_H

#include "camel-view-summary-disk.h"

typedef struct _CamelIMAPXViewSummary      CamelIMAPXViewSummary;
typedef struct _CamelIMAPXViewSummaryClass CamelIMAPXViewSummaryClass;

enum {
	CVS_IMAPX_SECTION_VIEWINFO = CVSD_SECTION_LAST,
};

typedef struct _CamelIMAPXView CamelIMAPXView;

struct _CamelIMAPXView {
	CamelViewDisk view;

	gchar separator;

	/* This data is only set on the root views */
	gchar *raw_name;
	guint32 exists;
	guint64 uidvalidityxx; /* Cope with it being 64-bit, if you ever build this file again */
	guint32 permanentflags;
};

struct _CamelIMAPXViewSummary {
	CamelViewSummaryDisk parent;
};

struct _CamelIMAPXViewSummaryClass {
	CamelViewSummaryDiskClass parent_class;
};

CamelType		camel_imapx_view_summary_get_type	(void);
CamelIMAPXViewSummary      *camel_imapx_view_summary_new	(const gchar *base, CamelException *ex);

/* called on root view */
guint32 camel_imapx_view_next_uid(CamelIMAPXView *view);
void camel_imapx_view_last_uid(CamelIMAPXView *view, guint32 uid);

#endif /* _CAMEL_IMAPX_VIEW_SUMMARY_H */

