/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-mime-part-utils : Utility for mime parsing and so on
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This library is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 * Authors: Bertrand Guiheneuf <bertrand@helixcode.com>
 */

#if !defined (__CAMEL_H_INSIDE__) && !defined (CAMEL_COMPILATION)
#error "Only <camel/camel.h> can be included directly."
#endif

#ifndef CAMEL_MIME_PART_UTILS_H
#define CAMEL_MIME_PART_UTILS_H

#include <camel/camel-mime-part.h>
#include <camel/camel-folder-summary.h>

G_BEGIN_DECLS

gboolean	camel_mime_part_construct_content_from_parser
						(CamelMimePart *mime_part,
						 CamelMimeParser *mp,
						 GCancellable *cancellable,
						 GError **error);
gboolean	camel_mime_message_build_preview (CamelMimePart *mime_part,
						 CamelMessageInfo *info);

G_END_DECLS

#endif /*  CAMEL_MIME_PART_UTILS_H  */
