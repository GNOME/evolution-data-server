/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-nntp-grouplist.h : getting/updating the list of newsgroups on the server. */

/*
 * Author : Chris Toshok <toshok@ximian.com>
 *
 * Copyright (C) 2000 Ximian .
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301
 * USA
 */

#ifndef CAMEL_NNTP_GROUPLIST_H
#define CAMEL_NNTP_GROUPLIST_H 1

#include <time.h>
#include "camel-nntp-store.h"

G_BEGIN_DECLS

struct CamelNNTPGroupListEntry {
	char *group_name;
	guint32 low;
	guint32 high;
	guint32 flags;
};

struct CamelNNTPGroupList {
	CamelNNTPStore *store;
	time_t time;
	GList *group_list;
};

typedef struct CamelNNTPGroupList _CamelNNTPGroupList;
typedef struct CamelNNTPGroupListEntry _CamelNNTPGroupListEntry;

struct CamelNNTPGroupList* camel_nntp_grouplist_fetch  (CamelNNTPStore *store, CamelException *ex);
gint                       camel_nntp_grouplist_update (struct CamelNNTPGroupList *group_list, CamelException *ex);
void                       camel_nntp_grouplist_save   (struct CamelNNTPGroupList *group_list, CamelException *ex);
void                       camel_nntp_grouplist_free   (struct CamelNNTPGroupList *group_list);

G_END_DECLS

#endif /* CAMEL_NNTP_GROUPLIST_H */
