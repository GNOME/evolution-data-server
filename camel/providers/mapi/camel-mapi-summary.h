/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* 
 * Johnny Jacob <jjohnny@novell.com>
 *   
 * Copyright (C) 2007, Novell Inc.
 *
 * This program is free software; you can redistribute it and/or 
 * modify it under the terms of version 3 of the GNU Lesser General Public 
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef _CAMEL_MAPI_SUMMARY_H
#define _CAMEL_MAPI_SUMMARY_H

#include <camel/camel-folder-summary.h>
#include <camel/camel-exception.h>
#include <camel/camel-store.h>

#define CAMEL_MAPI_SUMMARY(obj)         CAMEL_CHECK_CAST (obj, camel_mapi_summary_get_type (), CamelMapiSummary)
#define CAMEL_MAPI_SUMMARY_CLASS(klass) CAMEL_CHECK_CLASS_CAST (klass, camel_mapi_summary_get_type (), CamelMapiSummaryClass)
#define CAMEL_IS_MAPI_SUMMARY(obj)      CAMEL_CHECK_TYPE (obj, camel_mapi_summary_get_type ())

G_BEGIN_DECLS

typedef struct _CamelMapiSummary CamelMapiSummary ;
typedef struct _CamelMapiSummaryClass CamelMapiSummaryClass ;
typedef struct _CamelMapiMessageInfo CamelMapiMessageInfo ;
typedef struct _CamelMapiMessageContentInfo CamelMapiMessageContentInfo ;

/* extra summary flags*/
enum {
	CAMEL_GW_MESSAGE_JUNK = 1<<17,
	CAMEL_GW_MESSAGE_NOJUNK = 1<<18,
};

struct _CamelMapiMessageInfo {
	CamelMessageInfoBase info;

	guint32 server_flags;
} ;


struct _CamelMapiMessageContentInfo {
	CamelMessageContentInfo info ;
} ; 


struct _CamelMapiSummary {
	CamelFolderSummary parent ;

	char *time_string;
	guint32 version ;
	guint32 validity ;
} ;


struct _CamelMapiSummaryClass {
	CamelFolderSummaryClass parent_class ;
} ;


CamelType camel_mapi_summary_get_type (void) ;

CamelFolderSummary *camel_mapi_summary_new (struct _CamelFolder *folder, const char *filename) ;

/* void camel_gw_summary_add_offline (CamelFolderSummary *summary, const char *uid, CamelMimeMessage *messgae, const CamelMessageInfo *info) ; */

/* void camel_gw_summary_add_offline_uncached (CamelFolderSummary *summary, const char *uid, const CamelMessageInfo *info) ; */
void mapi_summary_clear (CamelFolderSummary *summary, gboolean uncache);

G_END_DECLS

#endif /*_CAMEL_GW_SUMMARY_H*/
