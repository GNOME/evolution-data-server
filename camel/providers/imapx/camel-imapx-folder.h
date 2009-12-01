/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-imap-folder.h : Class for a IMAP folder */

/*
 * Authors: Michael Zucchi <notzed@ximian.com>
 *
 * Copyright (C) 2002 Ximian, Inc. (www.ximian.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */

#ifndef CAMEL_IMAPX_FOLDER_H
#define CAMEL_IMAPX_FOLDER_H 1

#ifdef __cplusplus
extern "C" {
#pragma }
#endif /* __cplusplus }*/

#include <camel/camel-folder.h>

#define CAMEL_IMAPX_FOLDER_TYPE     (camel_imapx_folder_get_type ())
#define CAMEL_IMAPX_FOLDER(obj)     (CAMEL_CHECK_CAST((obj), CAMEL_IMAPX_FOLDER_TYPE, CamelIMAPXFolder))
#define CAMEL_IMAPX_FOLDER_CLASS(k) (CAMEL_CHECK_CLASS_CAST ((k), CAMEL_IMAPX_FOLDER_TYPE, CamelIMAPXFolderClass))
#define CAMEL_IS_IMAPX_FOLDER(o)    (CAMEL_CHECK_TYPE((o), CAMEL_IMAPX_FOLDER_TYPE))

typedef struct _CamelIMAPXFolder {
	CamelFolder parent_object;

	gchar *raw_name;
	CamelFolderSearch *search;
	GMutex *search_lock;
} CamelIMAPXFolder;

typedef struct _CamelIMAPXFolderClass {
	CamelFolderClass parent_class;
} CamelIMAPXFolderClass;

/* Standard Camel function */
CamelType camel_imapx_folder_get_type (void);

/* public methods */
CamelFolder *camel_imapx_folder_new(CamelStore *parent, const gchar *path, const gchar *raw);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* CAMEL_IMAPX_FOLDER_H */
