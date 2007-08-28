/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-imap-wrapper.h: data wrapper for offline IMAP data */

/*
 * Copyright 2000 Ximian, Inc. (www.ximian.com)
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


#ifndef CAMEL_IMAP_WRAPPER_H
#define CAMEL_IMAP_WRAPPER_H 1

#include <camel/camel-data-wrapper.h>
#include "camel-imap-types.h"

#define CAMEL_IMAP_WRAPPER_TYPE     (camel_imap_wrapper_get_type ())
#define CAMEL_IMAP_WRAPPER(obj)     (CAMEL_CHECK_CAST((obj), CAMEL_IMAP_WRAPPER_TYPE, CamelImapWrapper))
#define CAMEL_IMAP_WRAPPER_CLASS(k) (CAMEL_CHECK_CLASS_CAST ((k), CAMEL_IMAP_WRAPPER_TYPE, CamelImapWrapperClass))
#define CAMEL_IS_IMAP_WRAPPER(o)    (CAMEL_CHECK_TYPE((o), CAMEL_IMAP_WRAPPER_TYPE))

G_BEGIN_DECLS

typedef struct {
	CamelDataWrapper parent_object;

	struct _CamelImapWrapperPrivate *priv;

	CamelImapFolder *folder;
	char *uid, *part_spec;
	CamelMimePart *part;
} CamelImapWrapper;

typedef struct {
	CamelDataWrapperClass parent_class;

} CamelImapWrapperClass;

/* Standard Camel function */
CamelType camel_imap_wrapper_get_type (void);

/* Constructor */
CamelDataWrapper *camel_imap_wrapper_new (CamelImapFolder *imap_folder,
					  CamelContentType *type,
					  CamelTransferEncoding encoding,
					  const char *uid,
					  const char *part_spec,
					  CamelMimePart *part);

G_END_DECLS

#endif /* CAMEL_DATA_WRAPPER_H */
