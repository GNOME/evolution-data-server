/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Authors: Jeffrey Stedfast <fejj@ximian.com>
 *
 *  Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 */

#ifndef CAMEL_IMAP_UTILS_H
#define CAMEL_IMAP_UTILS_H

#include <sys/types.h>
#include <camel/camel.h>

#include "camel-imap-store.h"

G_BEGIN_DECLS

const gchar *imap_next_word (const gchar *buf);

struct _namespace {
	struct _namespace *next;
	gchar *prefix;
	gchar delim;
};

struct _namespaces {
	struct _namespace *personal;
	struct _namespace *other;
	struct _namespace *shared;
};

void imap_namespaces_destroy (struct _namespaces *namespaces);
struct _namespaces *imap_parse_namespace_response (const gchar *response);

gboolean imap_parse_list_response  (CamelImapStore *store, const gchar *buf, gint *flags,
				    gchar *sep, gchar **folder);

gchar   **imap_parse_folder_name    (CamelImapStore *store, const gchar *folder_name);

gchar    *imap_create_flag_list     (guint32 flags, CamelMessageInfo *info, guint32 permanent_flags);
gboolean imap_parse_flag_list      (gchar **flag_list_p, guint32 *flags_out, gchar **custom_flags_out);

enum { IMAP_STRING, IMAP_NSTRING, IMAP_ASTRING };

gchar    *imap_parse_string_generic (const gchar **str_p, gsize *len, gint type);

#define imap_parse_string(str_p, len_p) \
	imap_parse_string_generic (str_p, len_p, IMAP_STRING)
#define imap_parse_nstring(str_p, len_p) \
	imap_parse_string_generic (str_p, len_p, IMAP_NSTRING)
#define imap_parse_astring(str_p, len_p) \
	imap_parse_string_generic (str_p, len_p, IMAP_ASTRING)

void     imap_parse_body           (const gchar **body_p, CamelFolder *folder,
				    CamelMessageContentInfo *ci);

gboolean imap_is_atom              (const gchar *in);
gchar    *imap_quote_string         (const gchar *str);

void     imap_skip_list            (const gchar **str_p);

gchar    *imap_uid_array_to_set     (CamelFolderSummary *summary, GPtrArray *uids, gint uid, gssize maxlen, gint *lastuid);
GPtrArray *imap_uid_set_to_array   (CamelFolderSummary *summary, const gchar *uids);
void     imap_uid_array_free       (GPtrArray *arr);

gchar *imap_concat (CamelImapStore *imap_store, const gchar *prefix, const gchar *suffix);
gchar *imap_namespace_concat (CamelImapStore *store, const gchar *name);

gchar *imap_mailbox_encode (const guchar *in, gsize inlen);
gchar *imap_mailbox_decode (const guchar *in, gsize inlen);

typedef gboolean (*IMAPPathFindFoldersCallback) (const gchar *physical_path, const gchar *path, gpointer user_data);

gchar *imap_path_to_physical (const gchar *prefix, const gchar *vpath);
gboolean imap_path_find_folders (const gchar *prefix, IMAPPathFindFoldersCallback callback, gpointer data);

G_END_DECLS

#endif /* CAMEL_IMAP_UTILS_H */
