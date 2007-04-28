/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-imap-types.h: IMAP types */

/* 
 * Copyright (C) 2001 Ximian, Inc.
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */

#ifndef CAMEL_IMAP_TYPES_H
#define CAMEL_IMAP_TYPES_H 1

#include "camel-types.h"

G_BEGIN_DECLS

typedef struct _CamelImapFolder       CamelImapFolder;
typedef struct _CamelImapMessageCache CamelImapMessageCache;
typedef struct _CamelImapResponse     CamelImapResponse;
typedef struct _CamelImapSearch       CamelImapSearch;
typedef struct _CamelImapStore        CamelImapStore;
typedef struct _CamelImapSummary      CamelImapSummary;

G_END_DECLS

#endif /* CAMEL_IMAP_TYPES_H */
