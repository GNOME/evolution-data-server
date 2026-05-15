/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 * SPDX-FileContributor: Bertrand Guiheneuf <bertrand@helixcode.com>
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

typedef struct _CamelMessageContentInfo CamelMessageContentInfo;

/**
 * CamelMessageContentInfoTraverseCallback:
 * @ci: a #CamelMessageContentInfo
 * @depth: the current depth
 * @user_data: data passed to camel_message_content_info_traverse()
 *
 * This is the callback signature for camel_message_content_info_traverse().
 *
 * Returns: %TRUE to continue processing or %FALSE to stop it.
 *
 * Since: 3.36
 **/
typedef gboolean	(*CamelMessageContentInfoTraverseCallback)	(CamelMessageContentInfo *ci,
									 gint depth,
									 gpointer user_data);

/* A tree of message content info structures
 * describe the content structure of the message (if it has any) */
struct _CamelMessageContentInfo {
	CamelMessageContentInfo *next;

	CamelMessageContentInfo *childs;
	CamelMessageContentInfo *parent;

	CamelContentType *type;
	CamelContentDisposition *disposition;
	gchar *id;
	gchar *description;
	gchar *encoding;
	guint32 size;
};

GType		camel_message_content_info_get_type
						(void) G_GNUC_CONST;
CamelMessageContentInfo *
		camel_message_content_info_new	(void);
CamelMessageContentInfo *
		camel_message_content_info_copy	(const CamelMessageContentInfo *src);
void		camel_message_content_info_free	(CamelMessageContentInfo *ci);
CamelMessageContentInfo *
		camel_message_content_info_new_from_headers
						(const CamelNameValueArray *headers);
CamelMessageContentInfo *
		camel_message_content_info_new_from_parser
						(CamelMimeParser *parser);
CamelMessageContentInfo *
		camel_message_content_info_new_from_message
						(CamelMimePart *mime_part);
gboolean	camel_message_content_info_traverse
						(CamelMessageContentInfo *ci,
						 CamelMessageContentInfoTraverseCallback func,
						 gpointer user_data);
/* debugging functions */
void		camel_message_content_info_dump	(CamelMessageContentInfo *ci,
						 gint depth);

G_END_DECLS

#endif /*  CAMEL_MIME_PART_UTILS_H  */
