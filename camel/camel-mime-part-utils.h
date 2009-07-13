/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-mime-part-utils : Utility for mime parsing and so on */

/*
 *
 * Author :
 *  Bertrand Guiheneuf <bertrand@helixcode.com>
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
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

#ifndef CAMEL_MIME_PART_UTILS_H
#define CAMEL_MIME_PART_UTILS_H 1

#include <camel/camel-mime-part.h>
#include <camel/camel-folder-summary.h>

G_BEGIN_DECLS

void camel_mime_part_construct_content_from_parser(CamelMimePart *, CamelMimeParser *mp);
gboolean camel_mime_message_build_preview (CamelMimePart *msg, CamelMessageInfo *info);

G_END_DECLS

#endif /*  CAMEL_MIME_PART_UTILS_H  */
