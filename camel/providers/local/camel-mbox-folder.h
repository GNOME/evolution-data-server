/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Authors: Michael Zucchi <notzed@ximian.com>
 *
 * Copyright (C) 1999 Ximian .
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

#ifndef CAMEL_MBOX_FOLDER_H
#define CAMEL_MBOX_FOLDER_H 1

#include "camel-local-folder.h"
#include "camel-mbox-summary.h"

#define CAMEL_MBOX_FOLDER_TYPE     (camel_mbox_folder_get_type ())
#define CAMEL_MBOX_FOLDER(obj)     (CAMEL_CHECK_CAST((obj), CAMEL_MBOX_FOLDER_TYPE, CamelMboxFolder))
#define CAMEL_MBOX_FOLDER_CLASS(k) (CAMEL_CHECK_CLASS_CAST ((k), CAMEL_MBOX_FOLDER_TYPE, CamelMboxFolderClass))
#define CAMEL_IS_MBOX_FOLDER(o)    (CAMEL_CHECK_TYPE((o), CAMEL_MBOX_FOLDER_TYPE))

G_BEGIN_DECLS

typedef struct {
	CamelLocalFolder parent_object;

	int lockfd;		/* for when we have a lock on the folder */
} CamelMboxFolder;

typedef struct {
	CamelLocalFolderClass parent_class;

	/* Virtual methods */	
	
} CamelMboxFolderClass;

/* public methods */
/* flags are taken from CAMEL_STORE_FOLDER_* flags */
CamelFolder *camel_mbox_folder_new(CamelStore *parent_store, const char *full_name, guint32 flags, CamelException *ex);

/* Standard Camel function */
CamelType camel_mbox_folder_get_type(void);

G_END_DECLS

#endif /* CAMEL_MBOX_FOLDER_H */
