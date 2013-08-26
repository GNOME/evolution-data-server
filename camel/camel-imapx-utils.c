/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include <ctype.h>
#include <errno.h>
#include <string.h>

#include "camel-imapx-command.h"
#include "camel-imapx-folder.h"
#include "camel-imapx-settings.h"
#include "camel-imapx-stream.h"
#include "camel-imapx-summary.h"
#include "camel-imapx-store.h"
#include "camel-imapx-store-summary.h"
#include "camel-imapx-utils.h"

/* high-level parser state */
#define p(...) camel_imapx_debug(parse, __VA_ARGS__)
/* debug */
#define d(...) camel_imapx_debug(debug, __VA_ARGS__)

gint camel_imapx_debug_flags;
extern gint camel_verbose_debug;

#define debug_set_flag(flag) do { \
	if ((CAMEL_IMAPX_DEBUG_ALL & CAMEL_IMAPX_DEBUG_ ## flag) &&	\
	    camel_debug ("imapx:" #flag))				\
		camel_imapx_debug_flags |= CAMEL_IMAPX_DEBUG_ ## flag;	\
	} while (0)

static void camel_imapx_set_debug_flags (void)
{
	if (camel_verbose_debug || camel_debug ("imapx")) {
		camel_imapx_debug_flags = CAMEL_IMAPX_DEBUG_ALL;
		return;
	}

	debug_set_flag (command);
	debug_set_flag (debug);
	debug_set_flag (extra);
	debug_set_flag (io);
	debug_set_flag (token);
	debug_set_flag (parse);
}

#include "camel-imapx-tokenise.h"
#define SUBFOLDER_DIR_NAME     "subfolders"

#ifdef __GNUC__
__inline
#endif
camel_imapx_id_t
imapx_tokenise (register const gchar *str,
                register guint len)
{
	struct _imapx_keyword *k = imapx_tokenise_struct (str, len);

	if (k)
		return k->id;
	return 0;
}

static void imapx_namespace_clear (CamelIMAPXStoreNamespace **ns);
static const gchar * rename_label_flag (const gchar *flag, gint len, gboolean server_to_evo);

/* flag table */
static struct {
	const gchar *name;
	guint32 flag;
} flag_table[] = {
	{ "\\ANSWERED", CAMEL_MESSAGE_ANSWERED },
	{ "\\DELETED", CAMEL_MESSAGE_DELETED },
	{ "\\DRAFT", CAMEL_MESSAGE_DRAFT },
	{ "\\FLAGGED", CAMEL_MESSAGE_FLAGGED },
	{ "\\SEEN", CAMEL_MESSAGE_SEEN },
	{ "\\RECENT", CAMEL_IMAPX_MESSAGE_RECENT },
	{ "JUNK", CAMEL_MESSAGE_JUNK },
	{ "NOTJUNK", CAMEL_MESSAGE_NOTJUNK },
	{ "\\*", CAMEL_MESSAGE_USER }
};

/* utility functions
 * should this be part of imapx-driver? */
/* maybe this should be a stream op? */
gboolean
imapx_parse_flags (CamelIMAPXStream *stream,
                   guint32 *flagsp,
                   CamelFlag **user_flagsp,
                   GCancellable *cancellable,
                   GError **error)
{
	camel_imapx_token_t tok;
	guint len;
	guchar *token;
	guint32 flags = 0;

	*flagsp = flags;

	tok = camel_imapx_stream_token (
		stream, &token, &len, cancellable, error);

	if (tok == IMAPX_TOK_ERROR)
		return FALSE;

	if (tok != '(') {
		g_set_error (
			error, CAMEL_IMAPX_ERROR, 1,
			"expecting flag list");
		return FALSE;
	}

	do {
		tok = camel_imapx_stream_token (
			stream, &token, &len, cancellable, error);

		if (tok == IMAPX_TOK_ERROR)
			return FALSE;

		if (tok == IMAPX_TOK_TOKEN || tok == IMAPX_TOK_INT) {
			gboolean match_found = FALSE;
			gchar *upper;
			gint ii;

			upper = g_ascii_strup ((gchar *) token, len);

			for (ii = 0; ii < G_N_ELEMENTS (flag_table); ii++) {
				if (!strcmp (upper, flag_table[ii].name)) {
					flags |= flag_table[ii].flag;
					match_found = TRUE;
					break;
				}
			}

			if (!match_found && user_flagsp != NULL) {
				const gchar *flag_name;

				flag_name = rename_label_flag (
					(gchar *) token,
					strlen ((gchar *) token), TRUE);

				camel_flag_set (user_flagsp, flag_name, TRUE);
			}

			g_free (upper);

		} else if (tok != ')') {
			gboolean success;

			success = camel_imapx_stream_skip_until (
				stream, ")", cancellable, error);
			if (!success)
				return FALSE;
		}
	} while (tok != ')');

	*flagsp = flags;

	return TRUE;
}

/*
 * rename_flag
 * Converts label flag name on server to name used in Evolution or back.
 * if the flags does not match returns the original one as it is.
 * It will never return NULL, it will return empty string, instead.
 *
 * @flag: Flag to rename.
 * @len: Length of the flag name.
 * @server_to_evo: if TRUE, then converting server names to evo's names, if FALSE then opposite.
 */
static const gchar *
rename_label_flag (const gchar *flag,
                   gint len,
                   gboolean server_to_evo)
{
	gint i;
	const gchar *labels[] = {
		"$Label1", "$Labelimportant",
		"$Label2", "$Labelwork",
		"$Label3", "$Labelpersonal",
		"$Label4", "$Labeltodo",
		"$Label5", "$Labellater",
		NULL,      NULL };

	/* It really can pass zero-length flags inside, in that case it was able
	 * to always add first label, which is definitely wrong. */
	if (!len || !flag || !*flag)
		return "";

	for (i = 0 + (server_to_evo ? 0 : 1); labels[i]; i = i + 2) {
		if (!g_ascii_strncasecmp (flag, labels[i], len))
			return labels[i + (server_to_evo ? 1 : -1)];
	}

	return flag;
}

void
imapx_write_flags (GString *string,
                   guint32 flags,
                   CamelFlag *user_flags)
{
	gint i;
	gboolean first = TRUE;

	g_string_append_c (string, '(');

	for (i = 0; flags != 0 && i< G_N_ELEMENTS (flag_table); i++) {
		if (flag_table[i].flag & flags) {
			if (flags & CAMEL_IMAPX_MESSAGE_RECENT)
				continue;
			if (!first)
				g_string_append_c (string, ' ');
			first = FALSE;
			g_string_append (string, flag_table[i].name);

			flags &= ~flag_table[i].flag;
		}
	}

	while (user_flags) {
		const gchar *flag_name;

		flag_name = rename_label_flag (
			user_flags->name, strlen (user_flags->name), FALSE);

		if (!first)
			g_string_append_c (string, ' ');
		first = FALSE;
		g_string_append (string, flag_name);

		user_flags = user_flags->next;
	}

	g_string_append_c (string, ')');
}

static gboolean
imapx_update_user_flags (CamelMessageInfo *info,
                         CamelFlag *server_user_flags)
{
	gboolean changed = FALSE;
	CamelMessageInfoBase *binfo = (CamelMessageInfoBase *) info;
	CamelIMAPXMessageInfo *xinfo = (CamelIMAPXMessageInfo *) info;
	gboolean set_cal = FALSE;

	if (camel_flag_get (&binfo->user_flags, "$has_cal"))
		set_cal = TRUE;

	changed = camel_flag_list_copy (&binfo->user_flags, &server_user_flags);
	camel_flag_list_copy (&xinfo->server_user_flags, &server_user_flags);

	/* reset the calendar flag if it was set in messageinfo before */
	if (set_cal)
		camel_flag_set (&binfo->user_flags, "$has_cal", TRUE);

	return changed;
}

gboolean
imapx_update_message_info_flags (CamelMessageInfo *info,
                                 guint32 server_flags,
                                 CamelFlag *server_user_flags,
                                 guint32 permanent_flags,
                                 CamelFolder *folder,
                                 gboolean unsolicited)
{
	gboolean changed = FALSE;
	CamelIMAPXMessageInfo *xinfo = (CamelIMAPXMessageInfo *) info;

	if (server_flags != xinfo->server_flags) {
		guint32 server_set, server_cleared;

		server_set = server_flags & ~xinfo->server_flags;
		server_cleared = xinfo->server_flags & ~server_flags;

		/* Don't clear non-permanent server-side flags.
		 * This avoids overwriting local flags that we
		 * do store permanently, such as junk flags. */
		if (permanent_flags > 0)
			server_cleared &= permanent_flags;

		camel_message_info_set_flags ((
			CamelMessageInfo *) xinfo,
			server_set | server_cleared,
			(xinfo->info.flags | server_set) & ~server_cleared);

		xinfo->server_flags = server_flags;
		xinfo->info.flags = xinfo->info.flags & ~CAMEL_MESSAGE_FOLDER_FLAGGED;
		xinfo->info.dirty = TRUE;

		changed = TRUE;
	}

	if ((folder->permanent_flags & CAMEL_MESSAGE_USER) != 0 && imapx_update_user_flags (info, server_user_flags))
		changed = TRUE;

	return changed;
}

void
imapx_set_message_info_flags_for_new_message (CamelMessageInfo *info,
                                              guint32 server_flags,
                                              CamelFlag *server_user_flags,
                                              CamelFolder *folder)
{
	CamelMessageInfoBase *binfo = (CamelMessageInfoBase *) info;
	CamelIMAPXMessageInfo *xinfo = (CamelIMAPXMessageInfo *) info;

	binfo->flags |= server_flags;
	camel_message_info_set_flags (info, server_flags, binfo->flags | server_flags);

	xinfo->server_flags = server_flags;

	if (folder->permanent_flags & CAMEL_MESSAGE_USER)
		imapx_update_user_flags (info, server_user_flags);

	binfo->flags &= ~CAMEL_MESSAGE_FOLDER_FLAGGED;
	binfo->dirty = TRUE;
}

void
imapx_update_store_summary (CamelFolder *folder)
{
	CamelStoreInfo *si;
	CamelService *service;
	CamelStore *parent_store;
	CamelStoreSummary *store_summary;
	CamelSettings *settings;
	CamelIMAPXStore *imapx_store;
	CamelIMAPXFolder *imapx_folder;
	const gchar *full_name;
	gboolean mobile_mode;
	guint32 total;
	guint32 unread;

	g_return_if_fail (CAMEL_IS_IMAPX_FOLDER (folder));

	full_name = camel_folder_get_full_name (folder);
	parent_store = camel_folder_get_parent_store (folder);
	service = CAMEL_SERVICE (parent_store);

	settings = camel_service_ref_settings (service);
	mobile_mode = camel_imapx_settings_get_mobile_mode (
		CAMEL_IMAPX_SETTINGS (settings));
	g_object_unref (settings);

	imapx_folder = CAMEL_IMAPX_FOLDER (folder);
	imapx_store = CAMEL_IMAPX_STORE (parent_store);

	store_summary = CAMEL_STORE_SUMMARY (imapx_store->summary);

	si = camel_store_summary_path (store_summary, full_name);
	if (si == NULL)
		return;

	total = camel_folder_summary_count (folder->summary);
	unread = camel_folder_summary_get_unread_count (folder->summary);

	if (si->unread != unread || si->total != total) {

		/* XXX Why is this different for mobile mode? */
		if (mobile_mode)
			si->unread = imapx_folder->unread_on_server;
		else
			si->unread = unread;
		si->total = total;

		camel_store_summary_touch (store_summary);
		camel_store_summary_save (store_summary);
	}
}

gchar *
camel_imapx_dup_uid_from_summary_index (CamelFolder *folder,
                                        guint summary_index)
{
	CamelFolderSummary *summary;
	GPtrArray *array;
	gchar *uid = NULL;

	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), NULL);

	summary = folder->summary;
	g_return_val_if_fail (CAMEL_IS_FOLDER_SUMMARY (summary), NULL);

	array = camel_folder_summary_get_array (summary);
	g_return_val_if_fail (array != NULL, NULL);

	if (summary_index < array->len) {
		folder = camel_folder_summary_get_folder (summary);
		camel_folder_sort_uids (folder, array);
		uid = g_strdup (g_ptr_array_index (array, summary_index));
	}

	camel_folder_summary_free_array (array);

	return uid;
}

/*
 * capability_data ::= "CAPABILITY" SPACE [1#capability SPACE] "IMAP4rev1"
 *                     [SPACE 1#capability]
 *                 ;; IMAP4rev1 servers which offer RFC 1730
 *                 ;; compatibility MUST list "IMAP4" as the first
 *                 ;; capability.
 */

struct {
	const gchar *name;
	guint32 flag;
} capa_table[] = { /* used to create capa_htable only */
	{ "IMAP4", IMAPX_CAPABILITY_IMAP4 },
	{ "IMAP4REV1", IMAPX_CAPABILITY_IMAP4REV1 },
	{ "STATUS",  IMAPX_CAPABILITY_STATUS } ,
	{ "NAMESPACE", IMAPX_CAPABILITY_NAMESPACE },
	{ "UIDPLUS",  IMAPX_CAPABILITY_UIDPLUS },
	{ "LITERAL+", IMAPX_CAPABILITY_LITERALPLUS },
	{ "STARTTLS", IMAPX_CAPABILITY_STARTTLS },
	{ "IDLE", IMAPX_CAPABILITY_IDLE },
	{ "CONDSTORE", IMAPX_CAPABILITY_CONDSTORE },
	{ "QRESYNC", IMAPX_CAPABILITY_QRESYNC },
	{ "LIST-EXTENDED", IMAPX_CAPABILITY_LIST_EXTENDED },
	{ "LIST-STATUS", IMAPX_CAPABILITY_LIST_STATUS },
	{ "QUOTA", IMAPX_CAPABILITY_QUOTA },
	{ "MOVE", IMAPX_CAPABILITY_MOVE }
};

static GMutex capa_htable_lock;         /* capabilities lookup table lock */
static GHashTable *capa_htable = NULL;  /* capabilities lookup table (extensible) */

static void
create_initial_capabilities_table (void)
{
	gint i = 0;

	/* call within g_init_once() only,
	 * or require table lock
	 */

	/* TODO add imapx_utils_uninit()
	 *      to free hash table
	 */
	capa_htable = g_hash_table_new_full (
		g_str_hash,
		g_str_equal,
		g_free,
		NULL);

	for (i = 0; i < G_N_ELEMENTS (capa_table); i++) {
		g_hash_table_insert (
			capa_htable,
			g_strdup (capa_table[i].name),
			GUINT_TO_POINTER (capa_table[i].flag));
	}
}

struct _capability_info *
imapx_parse_capability (CamelIMAPXStream *stream,
                        GCancellable *cancellable,
                        GError **error)
{
	gint tok;
	guint len;
	guchar *token, *p, c;
	gboolean free_token = FALSE;
	struct _capability_info * cinfo;
	GError *local_error = NULL;

	cinfo = g_malloc0 (sizeof (*cinfo));
	cinfo->auth_types = g_hash_table_new_full (g_str_hash, g_str_equal, (GDestroyNotify) g_free, NULL);

	/* FIXME: handle auth types */
	while ((tok = camel_imapx_stream_token (stream, &token, &len, cancellable, &local_error)) != '\n' &&
		local_error == NULL) {
		switch (tok) {
			case ']':
				/* Put it back so that imapx_untagged() isn't unhappy */
				camel_imapx_stream_ungettoken (stream, tok, token, len);
				return cinfo;
			case 43:
				token = (guchar *) g_strconcat ((gchar *) token, "+", NULL);
				free_token = TRUE;
			case IMAPX_TOK_TOKEN:
			case IMAPX_TOK_STRING:
				p = token;
				while ((c = *p))
					*p++ = toupper(c);
				if (!strncmp ((gchar *) token, "AUTH=", 5)) {
					g_hash_table_insert (
						cinfo->auth_types,
						g_strdup ((gchar *) token + 5),
						GINT_TO_POINTER (1));
					break;
				}
			case IMAPX_TOK_INT:
				d (stream->tagprefix, " cap: '%s'\n", token);
				cinfo->capa |= imapx_lookup_capability ((gchar *) token);
				if (free_token) {
					g_free (token);
					token = NULL;
				}
				free_token = FALSE;
				break;
			default:
				g_set_error (error, CAMEL_IMAPX_ERROR, 1, "capability: expecting name");
				break;
		}
	}

	if (local_error != NULL) {
		g_propagate_error (error, local_error);
		imapx_free_capability (cinfo);
		cinfo = NULL;
	}

	return cinfo;
}

void imapx_free_capability (struct _capability_info *cinfo)
{
	g_hash_table_destroy (cinfo->auth_types);
	g_free (cinfo);
}

guint32
imapx_register_capability (const gchar *capability)
{
	guint32 capa_id = 0;
	guint64 check_id = 0;
	GList *vals = NULL;
	GList *tmp_vals = NULL;

	g_return_val_if_fail (capability != NULL, 0);

	g_mutex_lock (&capa_htable_lock);

	/* we rely on IMAP being the first flag, non-zero value
	 * (1 << 0), so we can use GPOINTER_TO_UINT (NULL) as
	 * invalid value
	 */
	capa_id = GPOINTER_TO_UINT (
		g_hash_table_lookup (capa_htable, capability));
	if (capa_id > 0)
		goto exit;

	/* not yet there, find biggest flag so far */
	vals = g_hash_table_get_values (capa_htable);
	tmp_vals = vals;
	while (tmp_vals != NULL) {
		guint32 tmp_id = GPOINTER_TO_UINT (tmp_vals->data);
		if (capa_id < tmp_id)
			capa_id = tmp_id;
		tmp_vals = g_list_next (tmp_vals);
	}
	g_list_free (vals);

	/* shift-left biggest-so-far, sanity-check */
	check_id = (capa_id << 1);
	g_return_val_if_fail (check_id <= (guint64) G_MAXUINT32, 0);
	capa_id = (guint32) check_id;

	/* insert */
	g_hash_table_insert (
		capa_htable,
		g_strdup (capability),
		GUINT_TO_POINTER (capa_id));

 exit:
	g_mutex_unlock (&capa_htable_lock);

	return capa_id;
}

guint32
imapx_lookup_capability (const gchar *capability)
{
	gpointer data;

	g_return_val_if_fail (capability != NULL, 0);

	g_mutex_lock (&capa_htable_lock);

	data = g_hash_table_lookup (capa_htable, capability);

	g_mutex_unlock (&capa_htable_lock);

	return GPOINTER_TO_UINT (data);
}

struct _CamelIMAPXNamespaceList *
imapx_parse_namespace_list (CamelIMAPXStream *stream,
                            GCancellable *cancellable,
                            GError **error)
{
	CamelIMAPXStoreNamespace *namespaces[3], *node, *tail;
	CamelIMAPXNamespaceList *nsl = NULL;
	gint tok, i;
	guint len;
	guchar *token;
	gint n = 0;

	nsl = g_malloc0 (sizeof (CamelIMAPXNamespaceList));
	nsl->personal = NULL;
	nsl->shared = NULL;
	nsl->other = NULL;

	tok = camel_imapx_stream_token (stream, &token, &len, cancellable, NULL);
	do {
		namespaces[n] = NULL;
		tail = (CamelIMAPXStoreNamespace *) &namespaces[n];

		if (tok == '(') {
			tok = camel_imapx_stream_token (stream, &token, &len, cancellable, NULL);

			while (tok == '(') {
				tok = camel_imapx_stream_token (stream, &token, &len, cancellable, NULL);
				if (tok != IMAPX_TOK_STRING) {
					g_set_error (error, 1, CAMEL_IMAPX_ERROR, "namespace: expected a prefix string");
					goto exception;
				}

				node = g_new0 (CamelIMAPXStoreNamespace, 1);
				node->next = NULL;
				node->prefix = g_strdup ((gchar *) token);

				tok = camel_imapx_stream_token (stream, &token, &len, cancellable, NULL);

				if (tok == IMAPX_TOK_STRING) {
					if (strlen ((gchar *) token) == 1) {
						node->sep = *token;
					} else {
						if (*token)
							node->sep = node->prefix[strlen (node->prefix) - 1];
						else
							node->sep = '\0';
					}
				} else if (tok == IMAPX_TOK_TOKEN) {
					/* will a NIL be possible here? */
					node->sep = '\0';
				} else {
					g_set_error (error, CAMEL_IMAPX_ERROR, 1, "namespace: expected a string separtor");
					g_free (node->prefix);
					g_free (node);
					goto exception;
				}

				tail->next = node;
				tail = node;

				if (*node->prefix && node->prefix[strlen (node->prefix) -1] == node->sep)
					node->prefix[strlen (node->prefix) - 1] = '\0';

				if (!g_ascii_strncasecmp (node->prefix, "INBOX", 5) &&
						(node->prefix[6] == '\0' || node->prefix[6] == node->sep ))
					memcpy (node->prefix, "INBOX", 5);

				tok = camel_imapx_stream_token (stream, &token, &len, cancellable, NULL);
				if (tok != ')') {
					g_set_error (error, CAMEL_IMAPX_ERROR, 1, "namespace: expected a ')'");
					goto exception;
				}

				tok = camel_imapx_stream_token (stream, &token, &len, cancellable, NULL);
			}

			if (tok != ')') {
				g_set_error (error, CAMEL_IMAPX_ERROR, 1, "namespace: expected a ')'");
				goto exception;
			}

		} else if (tok == IMAPX_TOK_TOKEN && !strcmp ((gchar *) token, "NIL")) {
			namespaces[n] = NULL;
		} else {
			g_set_error (error, CAMEL_IMAPX_ERROR, 1, "namespace: expected either a '(' or NIL");
			goto exception;
		}

		tok = camel_imapx_stream_token (stream, &token, &len, cancellable, NULL);
		n++;
	} while (n < 3);

	nsl->personal = namespaces[0];
	nsl->shared = namespaces[1];
	nsl->other = namespaces[2];

	return nsl;
exception:
	g_free (nsl);
	for (i = 0; i < 3; i++)
		imapx_namespace_clear (&namespaces[i]);

	return NULL;
}

/*
 * body            ::= "(" body_type_1part / body_type_mpart ")"
 *
 * body_extension  ::= nstring / number / "(" 1#body_extension ")"
 *                     ;; Future expansion.  Client implementations
 *                     ;; MUST accept body_extension fields.  Server
 *                     ;; implementations MUST NOT generate
 *                     ;; body_extension fields except as defined by
 *                     ;; future standard or standards-track
 *                     ;; revisions of this specification.
 *
 * body_ext_1part  ::= body_fld_md5[SPACE body_fld_dsp
 *                 [SPACE body_fld_lang
 *                 [SPACE 1#body_extension]]]
 *                     ;; MUST NOT be returned on non-extensible
 *                     ;; "BODY" fetch
 *
 * body_ext_mpart  ::= body_fld_param
 *                 [SPACE body_fld_dsp SPACE body_fld_lang
 *                 [SPACE 1#body_extension]]
 *                     ;; MUST NOT be returned on non-extensible
 *                     ;; "BODY" fetch
 *
 * body_fields     ::= body_fld_param SPACE body_fld_id SPACE
 *                     body_fld_desc SPACE body_fld_enc SPACE
 *                     body_fld_octets
 *
 * body_fld_desc   ::= nstring
 *
 * body_fld_dsp    ::= "(" string SPACE body_fld_param ")" / nil
 *
 * body_fld_enc    ::= (<"> ("7BIT" / "8BIT" / "BINARY" / "BASE64"/
 *                     "QUOTED-PRINTABLE") <">) / string
 *
 * body_fld_id     ::= nstring
 *
 * body_fld_lang   ::= nstring / "(" 1#string ")"
 *
 * body_fld_lines  ::= number
 *
 * body_fld_md5    ::= nstring
 *
 * body_fld_octets ::= number
 *
 * body_fld_param  ::= "(" 1#(string SPACE string) ")" / nil
 *
 * body_type_1part ::= (body_type_basic / body_type_msg / body_type_text)
 *                 [SPACE body_ext_1part]
 *
 * body_type_basic ::= media_basic SPACE body_fields
 *                     ;; MESSAGE subtype MUST NOT be "RFC822"
 *
 * body_type_mpart ::= 1*body SPACE media_subtype
 *                 [SPACE body_ext_mpart]
 *
 * body_type_msg   ::= media_message SPACE body_fields SPACE envelope
 *                     SPACE body SPACE body_fld_lines
 *
 * body_type_text  ::= media_text SPACE body_fields SPACE body_fld_lines
 *
 * envelope        ::= "(" env_date SPACE env_subject SPACE env_from
 *                     SPACE env_sender SPACE env_reply_to SPACE env_to
 *                     SPACE env_cc SPACE env_bcc SPACE env_in_reply_to
 *                     SPACE env_message_id ")"
 *
 * env_bcc         ::= "(" 1*address ")" / nil
 *
 * env_cc          ::= "(" 1*address ")" / nil
 *
 * env_date        ::= nstring
 *
 * env_from        ::= "(" 1*address ")" / nil
 *
 * env_in_reply_to ::= nstring
 *
 * env_message_id  ::= nstring
 *
 * env_reply_to    ::= "(" 1*address ")" / nil
 *
 * env_sender      ::= "(" 1*address ")" / nil
 *
 * env_subject     ::= nstring
 *
 * env_to          ::= "(" 1*address ")" / nil
 *
 * media_basic     ::= (<"> ("APPLICATION" / "AUDIO" / "IMAGE" /
 *                     "MESSAGE" / "VIDEO") <">) / string)
 *                     SPACE media_subtype
 *                     ;; Defined in[MIME-IMT]
 *
 * media_message   ::= <"> "MESSAGE" <"> SPACE <"> "RFC822" <">
 *                     ;; Defined in[MIME-IMT]
 *
 * media_subtype   ::= string
 *                     ;; Defined in[MIME-IMT]
 *
 * media_text      ::= <"> "TEXT" <"> SPACE media_subtype
 *                     ;; Defined in[MIME-IMT]
 *
 *  ( "type" "subtype"  body_fields [envelope body body_fld_lines]
 *                              [body_fld_lines]
 *
 *  (("TEXT" "PLAIN" ("CHARSET"
 *                      "US-ASCII") NIL NIL "7BIT" 1152 23)("TEXT" "PLAIN"
 *                      ("CHARSET" "US-ASCII" "NAME" "cc.diff")
 *                      "<960723163407.20117h@cac.washington.edu>"
 *                      "Compiler diff" "BASE64" 4554 73) "MIXED"))
 *
 */

/*
struct _body_fields {
	CamelContentType *ct;
	gchar *msgid, *desc;
	CamelTransferEncoding encoding;
	guint32 size;
	};*/

void
imapx_free_body (struct _CamelMessageContentInfo *cinfo)
{
	struct _CamelMessageContentInfo *list, *next;

	list = cinfo->childs;
	while (list) {
		next = list->next;
		imapx_free_body (list);
		list = next;
	}

	if (cinfo->type)
		camel_content_type_unref (cinfo->type);
	g_free (cinfo->id);
	g_free (cinfo->description);
	g_free (cinfo->encoding);
	g_free (cinfo);
}

gboolean
imapx_parse_param_list (CamelIMAPXStream *is,
                        struct _camel_header_param **plist,
                        GCancellable *cancellable,
                        GError **error)
{
	gint tok;
	guint len;
	guchar *token;
	gchar *param;

	p (is->tagprefix, "body_fld_param\n");

	/* body_fld_param  ::= "(" 1#(string SPACE string) ")" / nil */
	tok = camel_imapx_stream_token (is, &token, &len, cancellable, NULL);
	if (tok == '(') {
		while (1) {
			tok = camel_imapx_stream_token (is, &token, &len, cancellable, NULL);
			if (tok == ')')
				break;
			camel_imapx_stream_ungettoken (is, tok, token, len);

			camel_imapx_stream_astring (is, &token, cancellable, NULL);
			param = alloca (strlen ((gchar *) token) + 1);
			strcpy (param, (gchar *) token);
			camel_imapx_stream_astring (is, &token, cancellable, NULL);
			camel_header_set_param (plist, param, (gchar *) token);
		}
	} /* else check nil?  no need */

	return TRUE;
}

struct _CamelContentDisposition *
imapx_parse_ext_optional (CamelIMAPXStream *is,
                          GCancellable *cancellable,
                          GError **error)
{
	gint tok;
	guint len;
	guchar *token;
	struct _CamelContentDisposition *dinfo = NULL;
	GError *local_error = NULL;

	/* this parses both extension types, from the body_fld_dsp onwards */
	/* although the grammars are different, they can be parsed the same way */

	/* body_ext_1part  ::= body_fld_md5 [SPACE body_fld_dsp
	 * [SPACE body_fld_lang
	 * [SPACE 1#body_extension]]]
	 *    ;; MUST NOT be returned on non-extensible
	 *    ;; "BODY" fetch */

	/* body_ext_mpart  ::= body_fld_param
	 * [SPACE body_fld_dsp SPACE body_fld_lang
	 * [SPACE 1#body_extension]]
	 *    ;; MUST NOT be returned on non-extensible
	 *    ;; "BODY" fetch */

	/* body_fld_dsp    ::= "(" string SPACE body_fld_param ")" / nil */

	tok = camel_imapx_stream_token (is, &token, &len, cancellable, NULL);
	switch (tok) {
		case '(':
			dinfo = g_malloc0 (sizeof (*dinfo));
			dinfo->refcount = 1;
			/* should be string */
			camel_imapx_stream_astring (is, &token, cancellable, NULL);

			dinfo->disposition = g_strdup ((gchar *) token);
			imapx_parse_param_list (is, &dinfo->params, cancellable, NULL);
		case IMAPX_TOK_TOKEN:
			d (is->tagprefix, "body_fld_dsp: NIL\n");
			break;
		default:
			g_set_error (error, CAMEL_IMAPX_ERROR, 1, "body_fld_disp: expecting nil or list");
			return NULL;
	}

	p (is->tagprefix, "body_fld_lang\n");

	/* body_fld_lang   ::= nstring / "(" 1#string ")" */

	/* we just drop the lang string/list, save it somewhere? */

	tok = camel_imapx_stream_token (is, &token, &len, cancellable, &local_error);
	switch (tok) {
		case '(':
			while (1) {
				tok = camel_imapx_stream_token (is, &token, &len, cancellable, &local_error);
				if (tok == ')') {
					break;
				} else if (tok != IMAPX_TOK_STRING) {
					g_clear_error (&local_error);
					g_set_error (&local_error, CAMEL_IMAPX_ERROR, 1, "expecting string");
					break;
				}
			}
			break;
		case IMAPX_TOK_TOKEN:
			d (is->tagprefix, "body_fld_lang = nil\n");
			/* treat as 'nil' */
			break;
		case IMAPX_TOK_STRING:
			/* we have a string */
			break;
		case IMAPX_TOK_LITERAL:
			/* we have a literal string */
			camel_imapx_stream_set_literal (is, len);
			while (camel_imapx_stream_getl (is, &token, &len, cancellable, NULL) > 0) {
				d (is->tagprefix, "Skip literal data '%.*s'\n", (gint) len, token);
			}
			break;

	}

	if (local_error != NULL) {
		g_propagate_error (error, local_error);
		if (dinfo)
			camel_content_disposition_unref (dinfo);
		dinfo = NULL;
	}

	return dinfo;
}

struct _CamelMessageContentInfo *
imapx_parse_body_fields (CamelIMAPXStream *is,
                         GCancellable *cancellable,
                         GError **error)
{
	guchar *token;
	gchar  *type;
	guint64 number;
	struct _CamelMessageContentInfo *cinfo;

	/* body_fields     ::= body_fld_param SPACE body_fld_id SPACE
	 * body_fld_desc SPACE body_fld_enc SPACE
	 * body_fld_octets */

	p (is->tagprefix, "body_fields\n");

	cinfo = g_malloc0 (sizeof (*cinfo));

	/* this should be string not astring */
	if (!camel_imapx_stream_astring (is, &token, cancellable, error))
		goto error;
	type = alloca (strlen ((gchar *) token) + 1);
	strcpy (type, (gchar *) token);
	if (!camel_imapx_stream_astring (is, &token, cancellable, error))
		goto error;
	cinfo->type = camel_content_type_new (type, (gchar *) token);
	if (!imapx_parse_param_list (is, &cinfo->type->params, cancellable, error))
		goto error;

	/* body_fld_id     ::= nstring */
	if (!camel_imapx_stream_nstring (is, &token, cancellable, error))
		goto error;
	cinfo->id = g_strdup ((gchar *) token);

	/* body_fld_desc   ::= nstring */
	if (!camel_imapx_stream_nstring (is, &token, cancellable, error))
		goto error;
	cinfo->description = g_strdup ((gchar *) token);

	/* body_fld_enc    ::= (<"> ("7BIT" / "8BIT" / "BINARY" / "BASE64"/
	 * "QUOTED-PRINTABLE") <">) / string */
	if (!camel_imapx_stream_astring (is, &token, cancellable, error))
		goto error;
	cinfo->encoding = g_strdup ((gchar *) token);

	/* body_fld_octets ::= number */
	if (!camel_imapx_stream_number (is, &number, cancellable, error))
		goto error;
	cinfo->size = number;

	return cinfo;

error:
	imapx_free_body (cinfo);

	return cinfo;
}

struct _camel_header_address *
imapx_parse_address_list (CamelIMAPXStream *is,
                          GCancellable *cancellable,
                          GError **error)
/* throws PARSE,IO exception */
{
	gint tok;
	guint len;
	guchar *token, *host;
	gchar *mbox;
	struct _camel_header_address *list = NULL;
	GError *local_error = NULL;

	/* "(" 1*address ")" / nil */

	tok = camel_imapx_stream_token (is, &token, &len, cancellable, &local_error);
	if (tok == '(') {
		struct _camel_header_address *addr, *group = NULL;
		while (1) {
			/* address         ::= "(" addr_name SPACE addr_adl SPACE addr_mailbox
			 * SPACE addr_host ")" */
			tok = camel_imapx_stream_token (is, &token, &len, cancellable, &local_error);
			if (tok == ')')
				break;
			if (tok != '(') {
				g_clear_error (&local_error);
				camel_header_address_list_clear (&list);
				g_set_error (error, CAMEL_IMAPX_ERROR, 1, "missing '(' for address");
				return NULL;
			}

			addr = camel_header_address_new ();
			addr->type = CAMEL_HEADER_ADDRESS_NAME;
			camel_imapx_stream_nstring (is, &token, cancellable, &local_error);
			addr->name = g_strdup ((gchar *) token);
			/* we ignore the route, nobody uses it in the real world */
			camel_imapx_stream_nstring (is, &token, cancellable, &local_error);

			/* [RFC-822] group syntax is indicated by a special
			 * form of address structure in which the host name
			 * field is NIL.  If the mailbox name field is also
			 * NIL, this is an end of group marker (semi-colon in
			 * RFC 822 syntax).  If the mailbox name field is
			 * non-NIL, this is a start of group marker, and the
			 * mailbox name field holds the group name phrase. */

			camel_imapx_stream_nstring (is,(guchar **) &mbox, cancellable, &local_error);
			mbox = g_strdup (mbox);
			camel_imapx_stream_nstring (is, &host, cancellable, &local_error);
			if (host == NULL) {
				if (mbox == NULL) {
					group = NULL;
				} else {
					d (is->tagprefix, "adding group '%s'\n", mbox);
					g_free (addr->name);
					addr->name = mbox;
					addr->type = CAMEL_HEADER_ADDRESS_GROUP;
					camel_header_address_list_append (&list, addr);
					group = addr;
				}
			} else {
				addr->v.addr = g_strdup_printf ("%s@%s", mbox? mbox :"", (const gchar *) host);
				g_free (mbox);
				d (is->tagprefix, "adding address '%s'\n", addr->v.addr);
				if (group != NULL)
					camel_header_address_add_member (group, addr);
				else
					camel_header_address_list_append (&list, addr);
			}
			do {
				tok = camel_imapx_stream_token (is, &token, &len, cancellable, &local_error);
			} while (tok != ')');
		}
	} else {
		d (is->tagprefix, "empty, nil '%s'\n", token);
	}

	/* CHEN TODO handle exception at required places */
	if (local_error != NULL)
		g_propagate_error (error, local_error);

	return list;
}

struct _CamelMessageInfo *
imapx_parse_envelope (CamelIMAPXStream *is,
                      GCancellable *cancellable,
                      GError **error)
{
	gint tok;
	guint len;
	guchar *token;
	struct _camel_header_address *addr, *addr_from;
	gchar *addrstr;
	struct _CamelMessageInfoBase *minfo;
	GError *local_error = NULL;

	/* envelope        ::= "(" env_date SPACE env_subject SPACE env_from
	 * SPACE env_sender SPACE env_reply_to SPACE env_to
	 * SPACE env_cc SPACE env_bcc SPACE env_in_reply_to
	 * SPACE env_message_id ")" */

	p (is->tagprefix, "envelope\n");

	minfo = (CamelMessageInfoBase *) camel_message_info_new (NULL);

	tok = camel_imapx_stream_token (is, &token, &len, cancellable, &local_error);
	if (tok != '(') {
		g_clear_error (&local_error);
		camel_message_info_free (minfo);
		g_set_error (error, CAMEL_IMAPX_ERROR, 1, "envelope: expecting '('");
		return NULL;
	}

	/* env_date        ::= nstring */
	camel_imapx_stream_nstring (is, &token, cancellable, &local_error);
	minfo->date_sent = camel_header_decode_date ((gchar *) token, NULL);

	/* env_subject     ::= nstring */
	camel_imapx_stream_nstring (is, &token, cancellable, &local_error);
	minfo->subject = camel_pstring_strdup ((gchar *) token);

	/* we merge from/sender into from, append should probably merge more smartly? */

	/* env_from        ::= "(" 1*address ")" / nil */
	addr_from = imapx_parse_address_list (is, cancellable, &local_error);

	/* env_sender      ::= "(" 1*address ")" / nil */
	addr = imapx_parse_address_list (is, cancellable, &local_error);
	if (addr_from) {
		camel_header_address_list_clear (&addr);
#if 0
		if (addr)
			camel_header_address_list_append_list (&addr_from, &addr);
#endif
	} else {
		if (addr)
			addr_from = addr;
	}

	if (addr_from) {
		addrstr = camel_header_address_list_format (addr_from);
		minfo->from = camel_pstring_strdup (addrstr);
		g_free (addrstr);
		camel_header_address_list_clear (&addr_from);
	}

	/* we dont keep reply_to */

	/* env_reply_to    ::= "(" 1*address ")" / nil */
	addr = imapx_parse_address_list (is, cancellable, &local_error);
	camel_header_address_list_clear (&addr);

	/* env_to          ::= "(" 1*address ")" / nil */
	addr = imapx_parse_address_list (is, cancellable, &local_error);
	if (addr) {
		addrstr = camel_header_address_list_format (addr);
		minfo->to = camel_pstring_strdup (addrstr);
		g_free (addrstr);
		camel_header_address_list_clear (&addr);
	}

	/* env_cc          ::= "(" 1*address ")" / nil */
	addr = imapx_parse_address_list (is, cancellable, &local_error);
	if (addr) {
		addrstr = camel_header_address_list_format (addr);
		minfo->cc = camel_pstring_strdup (addrstr);
		g_free (addrstr);
		camel_header_address_list_clear (&addr);
	}

	/* we dont keep bcc either */

	/* env_bcc         ::= "(" 1*address ")" / nil */
	addr = imapx_parse_address_list (is, cancellable, &local_error);
	camel_header_address_list_clear (&addr);

	/* FIXME: need to put in-reply-to into references hash list */

	/* env_in_reply_to ::= nstring */
	camel_imapx_stream_nstring (is, &token, cancellable, &local_error);

	/* FIXME: need to put message-id into message-id hash */

	/* env_message_id  ::= nstring */
	camel_imapx_stream_nstring (is, &token, cancellable, &local_error);

	tok = camel_imapx_stream_token (is, &token, &len, cancellable, &local_error);
	if (tok != ')') {
		g_clear_error (&local_error);
		camel_message_info_free (minfo);
		g_set_error (error, CAMEL_IMAPX_ERROR, 1, "expecting ')'");
		return NULL;
	}

	/* CHEN TODO handle exceptions better */
	if (local_error != NULL)
		g_propagate_error (error, local_error);

	return (CamelMessageInfo *) minfo;
}

struct _CamelMessageContentInfo *
imapx_parse_body (CamelIMAPXStream *is,
                  GCancellable *cancellable,
                  GError **error)
{
	gint tok;
	guint len;
	guchar *token;
	struct _CamelMessageContentInfo * cinfo = NULL;
	struct _CamelMessageContentInfo *subinfo, *last;
	struct _CamelContentDisposition * dinfo = NULL;
	GError *local_error = NULL;

	/* body            ::= "(" body_type_1part / body_type_mpart ")" */

	p (is->tagprefix, "body\n");

	tok = camel_imapx_stream_token (is, &token, &len, cancellable, &local_error);
	if (tok != '(') {
		g_set_error (error, CAMEL_IMAPX_ERROR, 1, "body: expecting '('");
		return NULL;
	}

	/* 1*body (optional for multiparts) */
	tok = camel_imapx_stream_token (is, &token, &len, cancellable, &local_error);
	camel_imapx_stream_ungettoken (is, tok, token, len);
	if (tok == '(') {
		/* body_type_mpart ::= 1*body SPACE media_subtype
		[SPACE body_ext_mpart] */

		cinfo = g_malloc0 (sizeof (*cinfo));
		last = (struct _CamelMessageContentInfo *) &cinfo->childs;
		do {
			subinfo = imapx_parse_body (is, cancellable, &local_error);
			last->next = subinfo;
			last = subinfo;
			subinfo->parent = cinfo;
			tok = camel_imapx_stream_token (is, &token, &len, cancellable, &local_error);
			camel_imapx_stream_ungettoken (is, tok, token, len);
		} while (tok == '(');

		d (is->tagprefix, "media_subtype\n");

		camel_imapx_stream_astring (is, &token, cancellable, &local_error);
		cinfo->type = camel_content_type_new ("multipart", (gchar *) token);

		/* body_ext_mpart  ::= body_fld_param
		 * [SPACE body_fld_dsp SPACE body_fld_lang
		 * [SPACE 1#body_extension]]
		 *    ;; MUST NOT be returned on non-extensible
		 *    ;; "BODY" fetch */

		d (is->tagprefix, "body_ext_mpart\n");

		tok = camel_imapx_stream_token (is, &token, &len, cancellable, &local_error);
		camel_imapx_stream_ungettoken (is, tok, token, len);
		if (tok == '(') {
			imapx_parse_param_list (is, &cinfo->type->params, cancellable, &local_error);

			/* body_fld_dsp    ::= "(" string SPACE body_fld_param ")" / nil */

			tok = camel_imapx_stream_token (is, &token, &len, cancellable, &local_error);
			camel_imapx_stream_ungettoken (is, tok, token, len);
			if (tok == '(' || tok == IMAPX_TOK_TOKEN) {
				dinfo = imapx_parse_ext_optional (is, cancellable, &local_error);
				/* other extension fields?, soaked up below */
			} else {
				camel_imapx_stream_ungettoken (is, tok, token, len);
			}
		}
	} else {
		/* body_type_1part ::= (body_type_basic / body_type_msg / body_type_text)
		 * [SPACE body_ext_1part]
		 *
		 * body_type_basic ::= media_basic SPACE body_fields
		 * body_type_text  ::= media_text SPACE body_fields SPACE body_fld_lines
		 * body_type_msg   ::= media_message SPACE body_fields SPACE envelope
		 * SPACE body SPACE body_fld_lines */

		d (is->tagprefix, "Single part body\n");

		cinfo = imapx_parse_body_fields (is, cancellable, &local_error);

		d (is->tagprefix, "envelope?\n");

		/* do we have an envelope following */
		tok = camel_imapx_stream_token (is, &token, &len, cancellable, &local_error);
		camel_imapx_stream_ungettoken (is, tok, token, len);
		if (tok == '(') {
			struct _CamelMessageInfo * minfo = NULL;

			/* what do we do with the envelope?? */
			minfo = imapx_parse_envelope (is, cancellable, &local_error);
			/* what do we do with the message content info?? */
			//((CamelMessageInfoBase *) minfo)->content = imapx_parse_body (is);
			camel_message_info_free (minfo);
			minfo = NULL;
			d (is->tagprefix, "Scanned envelope - what do i do with it?\n");
		}

		d (is->tagprefix, "fld_lines?\n");

		/* do we have fld_lines following? */
		tok = camel_imapx_stream_token (is, &token, &len, cancellable, &local_error);
		if (tok == IMAPX_TOK_INT) {
			d (is->tagprefix, "field lines: %s\n", token);
			tok = camel_imapx_stream_token (is, &token, &len, cancellable, &local_error);
		}
		camel_imapx_stream_ungettoken (is, tok, token, len);

		/* body_ext_1part  ::= body_fld_md5 [SPACE body_fld_dsp
		[SPACE body_fld_lang
		[SPACE 1#body_extension]]]
		 * ;; MUST NOT be returned on non - extensible
		 * ;; "BODY" fetch */

		d (is->tagprefix, "extension data?\n");

		if (tok != ')') {
			camel_imapx_stream_nstring (is, &token, cancellable, &local_error);

			d (is->tagprefix, "md5: %s\n", token ? (gchar *) token:"NIL");

			/* body_fld_dsp    ::= "(" string SPACE body_fld_param ")" / nil */

			tok = camel_imapx_stream_token (is, &token, &len, cancellable, &local_error);
			camel_imapx_stream_ungettoken (is, tok, token, len);
			if (tok == '(' || tok == IMAPX_TOK_TOKEN) {
				dinfo = imapx_parse_ext_optional (is, cancellable, &local_error);
				/* then other extension fields, soaked up below */
			}
		}
	}

	/* soak up any other extension fields that may be present */
	/* there should only be simple tokens, no lists */
	do {
		tok = camel_imapx_stream_token (is, &token, &len, cancellable, &local_error);
		if (tok != ')') {
			d (is->tagprefix, "Dropping extension data '%s'\n", token);
		}
	} while (tok != ')');

	/* CHEN TODO handle exceptions better */
	if (local_error != NULL) {
		g_propagate_error (error, local_error);
		if (cinfo)
			imapx_free_body (cinfo);
		if (dinfo)
			camel_content_disposition_unref (dinfo);
		return NULL;
	}

	/* FIXME: do something with the disposition, currently we have no way to pass it out? */
	if (dinfo)
		camel_content_disposition_unref (dinfo);

	return cinfo;
}

gchar *
imapx_parse_section (CamelIMAPXStream *is,
                     GCancellable *cancellable,
                     GError **error)
{
	gint tok;
	guint len;
	guchar *token;
	gchar * section = NULL;

	/* currently we only return the part within the [section] specifier
	 * any header fields are parsed, but dropped */

	/*
	 * section         ::= "[" [section_text /
	 * (nz_number *["." nz_number] ["." (section_text / "MIME")])] "]"
	 *
	 * section_text    ::= "HEADER" / "HEADER.FIELDS" [".NOT"]
	 * SPACE header_list / "TEXT"
	 */

	tok = camel_imapx_stream_token (is, &token, &len, cancellable, NULL);
	if (tok != '[') {
		g_set_error (error, CAMEL_IMAPX_ERROR, 1, "section: expecting '['");
		return NULL;
	}

	tok = camel_imapx_stream_token (is, &token, &len, cancellable, NULL);
	if (tok == IMAPX_TOK_INT || tok == IMAPX_TOK_TOKEN)
		section = g_strdup ((gchar *) token);
	else if (tok == ']') {
		section = g_strdup ("");
		camel_imapx_stream_ungettoken (is, tok, token, len);
	} else {
		g_set_error (error, CAMEL_IMAPX_ERROR, 1, "section: expecting token");
		return NULL;
	}

	/* header_list     ::= "(" 1#header_fld_name ")"
	 * header_fld_name ::= astring */

	/* we dont need the header specifiers */
	tok = camel_imapx_stream_token (is, &token, &len, cancellable, NULL);
	if (tok == '(') {
		do {
			tok = camel_imapx_stream_token (is, &token, &len, cancellable, NULL);
			if (tok == IMAPX_TOK_STRING || tok == IMAPX_TOK_TOKEN || tok == IMAPX_TOK_INT) {
				/* ?do something? */
			} else if (tok != ')') {
				g_set_error (error, CAMEL_IMAPX_ERROR, 1, "section: header fields: expecting string");
				g_free (section);
				return NULL;
			}
		} while (tok != ')');
		tok = camel_imapx_stream_token (is, &token, &len, cancellable, NULL);
	}

	if (tok != ']') {
		g_set_error (error, CAMEL_IMAPX_ERROR, 1, "section: expecting ']'");
		g_free (section);
		return NULL;
	}

	return section;
}

static guint64
imapx_parse_modseq (CamelIMAPXStream *is,
                    GCancellable *cancellable,
                    GError **error)
{
	guint64 modseq = 0;
	gint tok;
	guint len;
	guchar *token;

	tok = camel_imapx_stream_token (is, &token, &len, cancellable, error);

	if (tok == CAMEL_IMAPX_ERROR)
		return 0;

	if (tok != '(') {
		g_set_error (
			error, CAMEL_IMAPX_ERROR, 1,
			"fetch: expecting '('");
		return 0;
	}

	if (!camel_imapx_stream_number (is, &modseq, cancellable, error))
		return 0;

	tok = camel_imapx_stream_token (is, &token, &len, cancellable, error);

	if (tok == CAMEL_IMAPX_ERROR)
		return 0;

	if (tok != ')') {
		g_set_error (
			error, CAMEL_IMAPX_ERROR, 1,
			"fetch: expecting '('");
		return 0;
	}

	return modseq;
}

void
imapx_free_fetch (struct _fetch_info *finfo)
{
	if (finfo == NULL)
		return;

	if (finfo->body)
		g_object_unref (finfo->body);
	if (finfo->text)
		g_object_unref (finfo->text);
	if (finfo->header)
		g_object_unref (finfo->header);
	if (finfo->minfo)
		camel_message_info_free (finfo->minfo);
	if (finfo->cinfo)
		imapx_free_body (finfo->cinfo);
	camel_flag_list_free (&finfo->user_flags);
	g_free (finfo->date);
	g_free (finfo->section);
	g_free (finfo->uid);
	g_free (finfo);
}

/* debug, dump one out */
void
imapx_dump_fetch (struct _fetch_info *finfo)
{
	CamelStream *sout;
	gchar *string;
	gint fd;

	d ('?', "Fetch info:\n");
	if (finfo == NULL) {
		d ('?', "Empty\n");
		return;
	}

	fd = dup (1);
	sout = camel_stream_fs_new_with_fd (fd);
	if (finfo->body) {
		camel_stream_write_string (sout, "Body content:\n", NULL, NULL);
		camel_stream_write_to_stream (finfo->body, sout, NULL, NULL);
		g_seekable_seek (
			G_SEEKABLE (finfo->body),
			0, G_SEEK_SET, NULL, NULL);
	}
	if (finfo->text) {
		camel_stream_write_string (sout, "Text content:\n", NULL, NULL);
		camel_stream_write_to_stream (finfo->text, sout, NULL, NULL);
		g_seekable_seek (
			G_SEEKABLE (finfo->text),
			0, G_SEEK_SET, NULL, NULL);
	}
	if (finfo->header) {
		camel_stream_write_string (sout, "Header content:\n", NULL, NULL);
		camel_stream_write_to_stream (finfo->header, sout, NULL, NULL);
		g_seekable_seek (
			G_SEEKABLE (finfo->header),
			0, G_SEEK_SET, NULL, NULL);
	}
	if (finfo->minfo) {
		camel_stream_write_string (sout, "Message Info:\n", NULL, NULL);
		camel_message_info_dump (finfo->minfo);
	}
	if (finfo->cinfo) {
		camel_stream_write_string (sout, "Content Info:\n", NULL, NULL);
		//camel_content_info_dump (finfo->cinfo, 0);
	}
	if (finfo->got & FETCH_SIZE) {
		string = g_strdup_printf ("Size: %d\n", (gint) finfo->size);
		camel_stream_write_string (sout, string, NULL, NULL);
		g_free (string);
	}
	if (finfo->got & FETCH_BODY) {
		string = g_strdup_printf ("Offset: %d\n", (gint) finfo->offset);
		camel_stream_write_string (sout, string, NULL, NULL);
		g_free (string);
	}
	if (finfo->got & FETCH_FLAGS) {
		string = g_strdup_printf ("Flags: %08x\n", (gint) finfo->flags);
		camel_stream_write_string (sout, string, NULL, NULL);
		g_free (string);
	}
	if (finfo->date) {
		string = g_strdup_printf ("Data: '%s'\n", finfo->date);
		camel_stream_write_string (sout, string, NULL, NULL);
		g_free (string);
	}
	if (finfo->section) {
		string = g_strdup_printf ("Section: '%s'\n", finfo->section);
		camel_stream_write_string (sout, string, NULL, NULL);
		g_free (string);
	}
	if (finfo->uid) {
		string = g_strdup_printf ("UID: '%s'\n", finfo->uid);
		camel_stream_write_string (sout, string, NULL, NULL);
		g_free (string);
	}
	g_object_unref (sout);
}

static gboolean
imapx_parse_fetch_body (CamelIMAPXStream *is,
                        struct _fetch_info *finfo,
                        GCancellable *cancellable,
                        GError **error)
{
	camel_imapx_token_t tok;
	guchar *token;
	guint len;

	tok = camel_imapx_stream_token (is, &token, &len, cancellable, error);

	if (tok == IMAPX_TOK_ERROR)
		return FALSE;

	camel_imapx_stream_ungettoken (is, tok, token, len);

	if (tok == '(') {
		finfo->cinfo = imapx_parse_body (is, cancellable, error);

		if (finfo->cinfo != NULL)
			finfo->got |= FETCH_CINFO;

		return (finfo->cinfo != NULL);
	}

	if (tok == '[') {
		gboolean success;

		finfo->section = imapx_parse_section (is, cancellable, error);

		if (finfo->section == NULL)
			return FALSE;

		finfo->got |= FETCH_SECTION;

		tok = camel_imapx_stream_token (
			is, &token, &len, cancellable, error);

		if (tok == IMAPX_TOK_ERROR)
			return FALSE;

		if (token[0] == '<') {
			finfo->offset = g_ascii_strtoull (
				(gchar *) token + 1, NULL, 10);
		} else {
			camel_imapx_stream_ungettoken (is, tok, token, len);
		}

		success = camel_imapx_stream_nstring_stream (
			is, &finfo->body, cancellable, error);

		/* Sanity check. */
		g_return_val_if_fail (
			(success && (finfo->body != NULL)) ||
			(!success && (finfo->body == NULL)), FALSE);

		if (success)
			finfo->got |= FETCH_BODY;

		return success;
	}

	g_set_error (
		error, CAMEL_IMAPX_ERROR, 1,
		"unknown body response");

	return FALSE;
}

static gboolean
imapx_parse_fetch_bodystructure (CamelIMAPXStream *is,
                                 struct _fetch_info *finfo,
                                 GCancellable *cancellable,
                                 GError **error)
{
	finfo->cinfo = imapx_parse_body (is, cancellable, error);

	if (finfo->cinfo != NULL)
		finfo->got |= FETCH_CINFO;

	return (finfo->cinfo != NULL);
}

static gboolean
imapx_parse_fetch_envelope (CamelIMAPXStream *is,
                            struct _fetch_info *finfo,
                            GCancellable *cancellable,
                            GError **error)
{
	finfo->minfo = imapx_parse_envelope (is, cancellable, error);

	if (finfo->minfo != NULL)
		finfo->got |= FETCH_MINFO;

	return (finfo->minfo != NULL);
}

static gboolean
imapx_parse_fetch_flags (CamelIMAPXStream *is,
                         struct _fetch_info *finfo,
                         GCancellable *cancellable,
                         GError **error)
{
	gboolean success;

	success = imapx_parse_flags (
		is, &finfo->flags, &finfo->user_flags, cancellable, error);

	if (success)
		finfo->got |= FETCH_FLAGS;

	return success;
}

static gboolean
imapx_parse_fetch_internaldate (CamelIMAPXStream *is,
                                struct _fetch_info *finfo,
                                GCancellable *cancellable,
                                GError **error)
{
	guchar *token;

	if (!camel_imapx_stream_nstring (is, &token, cancellable, error))
		return FALSE;

	/* XXX Convert to Camel format? */
	finfo->date = g_strdup ((gchar *) token);
	finfo->got |= FETCH_DATE;

	return TRUE;
}

static gboolean
imapx_parse_fetch_modseq (CamelIMAPXStream *is,
                          struct _fetch_info *finfo,
                          GCancellable *cancellable,
                          GError **error)
{
	finfo->modseq = imapx_parse_modseq (is, cancellable, error);

	if (finfo->modseq > 0)
		finfo->got |= FETCH_MODSEQ;

	return (finfo->modseq > 0);
}

static gboolean
imapx_parse_fetch_rfc822_header (CamelIMAPXStream *is,
                                 struct _fetch_info *finfo,
                                 GCancellable *cancellable,
                                 GError **error)
{
	gboolean success;

	success = camel_imapx_stream_nstring_stream (
		is, &finfo->header, cancellable, error);

	/* Sanity check. */
	g_return_val_if_fail (
		(success && (finfo->header != NULL)) ||
		(!success && (finfo->header == NULL)), FALSE);

	if (success)
		finfo->got |= FETCH_HEADER;

	return success;
}

static gboolean
imapx_parse_fetch_rfc822_size (CamelIMAPXStream *is,
                               struct _fetch_info *finfo,
                               GCancellable *cancellable,
                               GError **error)
{
	guint64 number;

	if (!camel_imapx_stream_number (is, &number, cancellable, error))
		return FALSE;

	finfo->size = (guint32) number;
	finfo->got |= FETCH_SIZE;

	return TRUE;
}

static gboolean
imapx_parse_fetch_rfc822_text (CamelIMAPXStream *is,
                               struct _fetch_info *finfo,
                               GCancellable *cancellable,
                               GError **error)
{
	gboolean success;

	success = camel_imapx_stream_nstring_stream (
		is, &finfo->text, cancellable, error);

	/* Sanity check. */
	g_return_val_if_fail (
		(success && (finfo->text != NULL)) ||
		(!success && (finfo->text == NULL)), FALSE);

	if (success)
		finfo->got |= FETCH_TEXT;

	return success;
}

static gboolean
imapx_parse_fetch_uid (CamelIMAPXStream *is,
                       struct _fetch_info *finfo,
                       GCancellable *cancellable,
                       GError **error)
{
	camel_imapx_token_t tok;
	guchar *token;
	guint len;

	tok = camel_imapx_stream_token (is, &token, &len, cancellable, error);

	if (tok == IMAPX_TOK_ERROR)
		return FALSE;

	if (tok != IMAPX_TOK_INT) {
		g_set_error (
			error, CAMEL_IMAPX_ERROR, 1,
			"uid not integer");
		return FALSE;
	}

	finfo->uid = g_strdup ((gchar *) token);
	finfo->got |= FETCH_UID;

	return TRUE;
}

struct _fetch_info *
imapx_parse_fetch (CamelIMAPXStream *is,
                   GCancellable *cancellable,
                   GError **error)
{
	gint tok;
	guint len;
	guchar *token, *p, c;
	struct _fetch_info *finfo;

	finfo = g_malloc0 (sizeof (*finfo));

	tok = camel_imapx_stream_token (is, &token, &len, cancellable, NULL);

	if (tok == IMAPX_TOK_ERROR)
		goto fail;

	if (tok != '(') {
		g_set_error (
			error, CAMEL_IMAPX_ERROR, 1,
			"fetch: expecting '('");
		goto fail;
	}

	tok = camel_imapx_stream_token (is, &token, &len, cancellable, error);

	while (tok == IMAPX_TOK_TOKEN) {
		gboolean success = FALSE;

		p = token;
		while ((c=*p))
			*p++ = toupper(c);

		switch (imapx_tokenise ((gchar *) token, len)) {
			case IMAPX_BODY:
				success = imapx_parse_fetch_body (
					is, finfo, cancellable, error);
				break;

			case IMAPX_BODYSTRUCTURE:
				success = imapx_parse_fetch_bodystructure (
					is, finfo, cancellable, error);
				break;

			case IMAPX_ENVELOPE:
				success = imapx_parse_fetch_envelope (
					is, finfo, cancellable, error);
				break;

			case IMAPX_FLAGS:
				success = imapx_parse_fetch_flags (
					is, finfo, cancellable, error);
				break;

			case IMAPX_INTERNALDATE:
				success = imapx_parse_fetch_internaldate (
					is, finfo, cancellable, error);
				break;

			case IMAPX_MODSEQ:
				success = imapx_parse_fetch_modseq (
					is, finfo, cancellable, error);
				break;

			case IMAPX_RFC822_HEADER:
				success = imapx_parse_fetch_rfc822_header (
					is, finfo, cancellable, error);
				break;

			case IMAPX_RFC822_SIZE:
				success = imapx_parse_fetch_rfc822_size (
					is, finfo, cancellable, error);
				break;

			case IMAPX_RFC822_TEXT:
				success = imapx_parse_fetch_rfc822_text (
					is, finfo, cancellable, error);
				break;

			case IMAPX_UID:
				success = imapx_parse_fetch_uid (
					is, finfo, cancellable, error);
				break;

			default:
				g_set_error (
					error, CAMEL_IMAPX_ERROR, 1,
					"unknown body response");
				break;
		}

		if (!success)
			goto fail;

		tok = camel_imapx_stream_token (
			is, &token, &len, cancellable, error);
	}

	if (tok == IMAPX_TOK_ERROR)
		goto fail;

	if (tok != ')') {
		g_set_error (
			error, CAMEL_IMAPX_ERROR, 1,
			"missing closing ')' on fetch response");
		goto fail;
	}

	goto exit;

fail:
	imapx_free_fetch (finfo);
	finfo = NULL;

exit:
	return finfo;
}

static void
generate_uids_from_sequence (GPtrArray *uids,
                             guint32 begin_uid,
                             guint32 end_uid)
{
	guint32 i;

	for (i = begin_uid; i <= end_uid; i++)
		g_ptr_array_add (uids, GUINT_TO_POINTER (i));
}

GPtrArray *
imapx_parse_uids (CamelIMAPXStream *is,
                  GCancellable *cancellable,
                  GError **error)
{
	GPtrArray *uids;
	guchar *token;
	gchar **splits;
	guint len, str_len;
	gint tok, i;

	tok = camel_imapx_stream_token (is, &token, &len, cancellable, error);
	if (tok < 0)
		return NULL;

	uids = g_ptr_array_new ();
	splits = g_strsplit ((gchar *) token, ",", -1);
	str_len = g_strv_length (splits);

	for (i = 0; i < str_len; i++)	{
		if (g_strstr_len (splits[i], -1, ":")) {
			gchar **seq = g_strsplit (splits[i], ":", -1);
			guint32 uid1 = strtoul ((gchar *) seq[0], NULL, 10);
			guint32 uid2 = strtoul ((gchar *) seq[1], NULL, 10);

			generate_uids_from_sequence (uids, uid1, uid2);
			g_strfreev (seq);
		} else {
			guint32 uid = strtoul ((gchar *) splits[i], NULL, 10);
			g_ptr_array_add (uids, GUINT_TO_POINTER (uid));
		}
	}

	g_strfreev (splits);

	return uids;
}

static gboolean
imapx_parse_status_appenduid (CamelIMAPXStream *is,
                              struct _status_info *sinfo,
                              GCancellable *cancellable,
                              GError **error)
{
	guint64 number;

	if (!camel_imapx_stream_number (is, &number, cancellable, error))
		return FALSE;

	sinfo->u.appenduid.uidvalidity = number;

	if (!camel_imapx_stream_number (is, &number, cancellable, error))
		return FALSE;

	sinfo->u.appenduid.uid = (guint32) number;

	return TRUE;
}

static gboolean
imapx_parse_status_capability (CamelIMAPXStream *is,
                               struct _status_info *sinfo,
                               GCancellable *cancellable,
                               GError **error)
{
	sinfo->u.cinfo = imapx_parse_capability (is, cancellable, error);

	return (sinfo->u.cinfo != NULL);
}

static gboolean
imapx_parse_status_copyuid (CamelIMAPXStream *is,
                            struct _status_info *sinfo,
                            GCancellable *cancellable,
                            GError **error)
{
	GPtrArray *uids;
	guint64 number;

	if (!camel_imapx_stream_number (is, &number, cancellable, error))
		return FALSE;

	sinfo->u.copyuid.uidvalidity = number;

	uids = imapx_parse_uids (is, cancellable, error);
	if (uids == NULL)
		return FALSE;

	sinfo->u.copyuid.uids = uids;

	uids = imapx_parse_uids (is, cancellable, error);
	if (uids == NULL)
		return FALSE;

	sinfo->u.copyuid.copied_uids = uids;

	return TRUE;
}

static gboolean
imapx_parse_status_highestmodseq (CamelIMAPXStream *is,
                                  struct _status_info *sinfo,
                                  GCancellable *cancellable,
                                  GError **error)
{
	guint64 number;

	if (!camel_imapx_stream_number (is, &number, cancellable, error))
		return FALSE;

	sinfo->u.highestmodseq = number;

	return TRUE;
}

static gboolean
imapx_parse_status_newname (CamelIMAPXStream *is,
                            struct _status_info *sinfo,
                            GCancellable *cancellable,
                            GError **error)
{
	guchar *token;

	/* XXX The RFC doesn't specify the BNF grammer for this. */

	if (!camel_imapx_stream_astring (is, &token, cancellable, error))
		return FALSE;

	sinfo->u.newname.oldname = g_strdup ((gchar *) token);

	if (!camel_imapx_stream_astring (is, &token, cancellable, error))
		return FALSE;

	sinfo->u.newname.newname = g_strdup ((gchar *) token);

	return TRUE;
}

static gboolean
imapx_parse_status_permanentflags (CamelIMAPXStream *is,
                                   struct _status_info *sinfo,
                                   GCancellable *cancellable,
                                   GError **error)
{
	guint32 flags;

	/* We only care about \* for permanent flags, not user flags. */
	if (!imapx_parse_flags (is, &flags, NULL, cancellable, error))
		return FALSE;

	sinfo->u.permanentflags = flags;

	return TRUE;
}

static gboolean
imapx_parse_status_uidnext (CamelIMAPXStream *is,
                            struct _status_info *sinfo,
                            GCancellable *cancellable,
                            GError **error)
{
	guint64 number;

	if (!camel_imapx_stream_number (is, &number, cancellable, error))
		return FALSE;

	sinfo->u.uidnext = (guint32) number;

	return TRUE;
}

static gboolean
imapx_parse_status_uidvalidity (CamelIMAPXStream *is,
                                struct _status_info *sinfo,
                                GCancellable *cancellable,
                                GError **error)
{
	guint64 number;

	if (!camel_imapx_stream_number (is, &number, cancellable, error))
		return FALSE;

	sinfo->u.uidvalidity = number;

	return TRUE;
}

static gboolean
imapx_parse_status_unseen (CamelIMAPXStream *is,
                           struct _status_info *sinfo,
                           GCancellable *cancellable,
                           GError **error)
{
	guint64 number;

	if (!camel_imapx_stream_number (is, &number, cancellable, error))
		return FALSE;

	sinfo->u.unseen = (guint32) number;

	return TRUE;
}

/* rfc 2060 section 7.1 Status Responses */
/* should this start after [ or before the [? token_unget anyone? */
struct _status_info *
imapx_parse_status (CamelIMAPXStream *is,
                    GCancellable *cancellable,
                    GError **error)
{
	gint tok;
	guint len;
	guchar *token;
	struct _status_info *sinfo;

	if (!camel_imapx_stream_atom (is, &token, &len, cancellable, error))
		return NULL;

	sinfo = g_malloc0 (sizeof (*sinfo));

	/*
	 * resp_cond_auth  ::= ("OK" / "PREAUTH") SPACE resp_text
	 * ;; Authentication condition
	 *
	 * resp_cond_bye   ::= "BYE" SPACE resp_text
	 *
	 * resp_cond_state ::= ("OK" / "NO" / "BAD") SPACE resp_text
	 * ;; Status condition
	 */

	sinfo->result = imapx_tokenise ((gchar *) token, len);
	switch (sinfo->result) {
		case IMAPX_OK:
		case IMAPX_NO:
		case IMAPX_BAD:
		case IMAPX_PREAUTH:
		case IMAPX_BYE:
			break;
		default:
			g_set_error (
				error, CAMEL_IMAPX_ERROR, 1,
				"expecting OK/NO/BAD");
			goto fail;
	}

	tok = camel_imapx_stream_token (is, &token, &len, cancellable, error);

	if (tok == IMAPX_TOK_ERROR)
		goto fail;

	if (tok == '[') {
		gboolean success = TRUE;

		if (!camel_imapx_stream_atom (
			is, &token, &len, cancellable, error))
			goto fail;

		sinfo->condition = imapx_tokenise ((gchar *) token, len);

		/* parse any details */
		switch (sinfo->condition) {
			case IMAPX_READ_ONLY:
			case IMAPX_READ_WRITE:
			case IMAPX_ALERT:
			case IMAPX_PARSE:
			case IMAPX_TRYCREATE:
			case IMAPX_CLOSED:
				break;

			case IMAPX_APPENDUID:
				success = imapx_parse_status_appenduid (
					is, sinfo, cancellable, error);
				break;

			case IMAPX_CAPABILITY:
				success = imapx_parse_status_capability (
					is, sinfo, cancellable, error);
				break;

			case IMAPX_COPYUID:
				success = imapx_parse_status_copyuid (
					is, sinfo, cancellable, error);
				break;

			case IMAPX_HIGHESTMODSEQ:
				success = imapx_parse_status_highestmodseq (
					is, sinfo, cancellable, error);
				break;

			case IMAPX_NEWNAME:
				success = imapx_parse_status_newname (
					is, sinfo, cancellable, error);
				break;

			case IMAPX_PERMANENTFLAGS:
				success = imapx_parse_status_permanentflags (
					is, sinfo, cancellable, error);
				break;

			case IMAPX_UIDNEXT:
				success = imapx_parse_status_uidnext (
					is, sinfo, cancellable, error);
				break;

			case IMAPX_UIDVALIDITY:
				success = imapx_parse_status_uidvalidity (
					is, sinfo, cancellable, error);
				break;

			case IMAPX_UNSEEN:
				success = imapx_parse_status_unseen (
					is, sinfo, cancellable, error);
				break;

			/* RFC 5530 Response Codes */
			case IMAPX_ALREADYEXISTS:
			case IMAPX_AUTHENTICATIONFAILED:
			case IMAPX_AUTHORIZATIONFAILED:
			case IMAPX_CANNOT:
			case IMAPX_CLIENTBUG:
			case IMAPX_CONTACTADMIN:
			case IMAPX_CORRUPTION:
			case IMAPX_EXPIRED:
			case IMAPX_EXPUNGEISSUED:
			case IMAPX_INUSE:
			case IMAPX_LIMIT:
			case IMAPX_NONEXISTENT:
			case IMAPX_NOPERM:
			case IMAPX_OVERQUOTA:
			case IMAPX_PRIVACYREQUIRED:
			case IMAPX_SERVERBUG:
			case IMAPX_UNAVAILABLE:
				break;

			default:
				sinfo->condition = IMAPX_UNKNOWN;
				d (is->tagprefix, "Got unknown response code: %s: ignored\n", token);
		}

		if (!success)
			goto fail;

		/* ignore anything we dont know about */
		do {
			tok = camel_imapx_stream_token (is, &token, &len, cancellable, NULL);
			if (tok == '\n' || tok < 0) {
				g_set_error (
					error, CAMEL_IMAPX_ERROR, 1,
					"server response truncated");
				goto fail;
			}
		} while (tok != ']');
	} else {
		camel_imapx_stream_ungettoken (is, tok, token, len);
	}

	/* and take the human readable response */
	if (!camel_imapx_stream_text (
		is, (guchar **) &sinfo->text, cancellable, error))
		goto fail;

	goto exit;

fail:
	imapx_free_status (sinfo);
	sinfo = NULL;

exit:
	return sinfo;
}

struct _status_info *
imapx_copy_status (struct _status_info *sinfo)
{
	struct _status_info *out;

	out = g_malloc (sizeof (*out));
	memcpy (out, sinfo, sizeof (*out));
	out->text = g_strdup (out->text);
	if (out->condition == IMAPX_NEWNAME) {
		out->u.newname.oldname = g_strdup (out->u.newname.oldname);
		out->u.newname.newname = g_strdup (out->u.newname.newname);
	}

	return out;
}

void
imapx_free_status (struct _status_info *sinfo)
{
	if (sinfo == NULL)
		return;

	switch (sinfo->condition) {
	case IMAPX_NEWNAME:
		g_free (sinfo->u.newname.oldname);
		g_free (sinfo->u.newname.newname);
		break;
	case IMAPX_COPYUID:
		g_ptr_array_free (sinfo->u.copyuid.uids, FALSE);
		g_ptr_array_free (sinfo->u.copyuid.copied_uids, FALSE);
		break;
	case IMAPX_CAPABILITY:
		if (sinfo->u.cinfo)
			imapx_free_capability (sinfo->u.cinfo);
		break;
	default:
		break;
	}

	g_free (sinfo->text);
	g_free (sinfo);
}

gboolean
camel_imapx_command_add_qresync_parameter (CamelIMAPXCommand *ic,
                                           CamelFolder *folder)
{
	/* See RFC 5162 Section 3.1 */

	CamelIMAPXFolder *imapx_folder;
	CamelIMAPXSummary *imapx_summary;
	guint64 last_known_uidvalidity;
	guint64 last_known_modsequence;
	guint32 last_known_message_cnt;
	guint32 sequence_limit;
	gchar *known_uid_set = NULL;
	gint summary_total;
	gboolean parameter_added = FALSE;

	g_return_val_if_fail (CAMEL_IS_IMAPX_COMMAND (ic), FALSE);
	g_return_val_if_fail (CAMEL_IS_IMAPX_FOLDER (folder), FALSE);

	imapx_folder = CAMEL_IMAPX_FOLDER (folder);
	imapx_summary = CAMEL_IMAPX_SUMMARY (folder->summary);

	last_known_uidvalidity = imapx_folder->uidvalidity_on_server;
	last_known_modsequence = imapx_summary->modseq;
	last_known_message_cnt = imapx_folder->exists_on_server;

	/* XXX This should return an unsigned integer to
	 *     avoid the possibility of a negative count. */
	summary_total = camel_folder_summary_count (folder->summary);
	g_return_val_if_fail (summary_total >= 0, FALSE);

	if (summary_total > 0) {
		guint last = summary_total - 1;
		gchar *begin, *end;

		begin = camel_imapx_dup_uid_from_summary_index (folder, 0);
		end = camel_imapx_dup_uid_from_summary_index (folder, last);

		if (begin != NULL && end != NULL)
			known_uid_set = g_strconcat (begin, ":", end, NULL);

		g_free (begin);
		g_free (end);
	}

	/* Make sure we have valid QRESYNC arguments. */

	if (last_known_uidvalidity == 0)
		goto exit;

	if (last_known_modsequence == 0)
		goto exit;

	if (known_uid_set == NULL)
		goto exit;

	camel_imapx_command_add (
		ic, " (QRESYNC (%"
		G_GUINT64_FORMAT " %"
		G_GUINT64_FORMAT " %s",
		last_known_uidvalidity,
		last_known_modsequence,
		known_uid_set);

	/* Add message sequence match data if we have enough messages. */

	/* XXX Some IMAP servers like Zimbra can't handle invalid sequence
	 *     numbers in the optional seq/uid list.  So limit the list to
	 *     the lesser of the last known message count according to the
	 *     server and our own summary count. */
	sequence_limit = MIN (last_known_message_cnt, summary_total);

	if (sequence_limit > 10) {
		GString *seqs;
		GString *uids;
		guint32 ii = 3;

		seqs = g_string_sized_new (256);
		uids = g_string_sized_new (256);

		/* Include some seq/uid pairs to avoid a huge VANISHED list.
		 * Work backwards exponentially from the end of the mailbox,
		 * starting with message 9 from the end, then 27 from the
		 * end, then 81 from the end, etc. */
		do {
			guint32 summary_index;
			gchar buf[10];
			gchar *uid;

			ii = MIN (ii * 3, sequence_limit);
			summary_index = sequence_limit - ii;

			if (seqs->len > 0)
				g_string_prepend_c (seqs, ',');

			if (uids->len > 0)
				g_string_prepend_c (uids, ',');

			/* IMAP sequence numbers are 1-based,
			 * but our folder summary is 0-based. */
			sprintf (buf, "%" G_GUINT32_FORMAT, summary_index + 1);

			uid = camel_imapx_dup_uid_from_summary_index (
				folder, summary_index);
			if (uid != NULL) {
				g_string_prepend (seqs, buf);
				g_string_prepend (uids, uid);
				g_free (uid);
			}
		} while (ii < sequence_limit);

		camel_imapx_command_add (
			ic, " (%s %s)", seqs->str, uids->str);

		g_string_free (seqs, TRUE);
		g_string_free (uids, TRUE);
	}

	camel_imapx_command_add (ic, "))");

	parameter_added = TRUE;

exit:
	g_free (known_uid_set);

	return parameter_added;
}

/**
 * camel_imapx_parse_mailbox:
 * @is: a #CamelIMAPXStream
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Parses a "mailbox" token from @is, with the special case for INBOX as
 * described in <ulink url="http://tools.ietf.org/html/rfc3501#section-5.1">
 * RFC 3501 section 5.1</ulink>.
 *
 * Since: 3.10
 **/
gchar *
camel_imapx_parse_mailbox (CamelIMAPXStream *is,
                           GCancellable *cancellable,
                           GError **error)
{
	guchar *token;
	gchar *mailbox;

	g_return_val_if_fail (CAMEL_IS_IMAPX_STREAM (is), NULL);

	/* mailbox ::= "INBOX" / astring
	 *             INBOX is case-insensitive.  All case variants of
	 *             INBOX (e.g., "iNbOx") MUST be interpreted as INBOX
	 *             not as an astring.  An astring which consists of
	 *             the case-insensitive sequence "I" "N" "B" "O" "X"
	 *             is considered to be INBOX and not an astring. */

	if (!camel_imapx_stream_astring (is, &token, cancellable, error))
		return NULL;

	if (g_ascii_strcasecmp ((gchar *) token, "INBOX") == 0)
		mailbox = g_strdup ("INBOX");
	else
		mailbox = camel_utf7_utf8 ((gchar *) token);

	return mailbox;
}

/**
 * camel_imapx_mailbox_is_inbox:
 * @mailbox_name: a mailbox name
 *
 * Returns whether @mailbox_name is the special mailbox INBOX.
 *
 * The function just performs a case-insensitive string comparsion; it's
 * more for readability.
 *
 * Returns: %TRUE if @mailbox_name is INBOX, %FALSE if not
 *
 * Since: 3.10
 **/
gboolean
camel_imapx_mailbox_is_inbox (const gchar *mailbox_name)
{
	g_return_val_if_fail (mailbox_name != NULL, FALSE);

	return (g_ascii_strcasecmp (mailbox_name, "INBOX") == 0);
}

/**
 * camel_imapx_mailbox_to_folder_path:
 * @mailbox_name: a mailbox name
 * @separator: mailbox separator character
 *
 * Converts @mailbox_name to a Camel folder path, which just replaces all
 * @separator characters with '/'.  If '/' appears in @mailbox_name, it is
 * replaced with @separator.  Free the returned string with g_free().
 *
 * Returns: a newly-allocated Camel folder path
 *
 * Since: 3.10
 **/
gchar *
camel_imapx_mailbox_to_folder_path (const gchar *mailbox_name,
                                    gchar separator)
{
	gchar *folder_path;

	g_return_val_if_fail (mailbox_name != NULL, NULL);

	folder_path = g_strdup (mailbox_name);

	if (separator != '\0' && separator != '/') {
		gchar *cp = folder_path;

		while (*cp != '\0') {
			if (*cp == '/')
				*cp = separator;
			else if (*cp == separator)
				*cp = '/';
			cp++;
		}
	}

	return folder_path;
}

gboolean
camel_imapx_parse_quota (CamelIMAPXStream *is,
                         GCancellable *cancellable,
                         gchar **out_quota_root_name,
                         CamelFolderQuotaInfo **out_quota_info,
                         GError **error)
{
	GQueue queue = G_QUEUE_INIT;
	CamelFolderQuotaInfo *info;
	CamelFolderQuotaInfo *next;
	gint tok;
	guint len;
	guchar *token;
	gchar *quota_root_name = NULL;
	gchar *resource_name = NULL;
	guint64 resource_usage;
	guint64 resource_limit;

	g_return_val_if_fail (CAMEL_IS_IMAPX_STREAM (is), FALSE);
	g_return_val_if_fail (out_quota_root_name != NULL, FALSE);
	g_return_val_if_fail (out_quota_info != NULL, FALSE);

	/* quota_response  ::= "QUOTA" SP astring SP quota_list
	 * quota_list      ::= "(" #quota_resource ")"
	 * quota_resource  ::= atom SP number SP number */

	if (!camel_imapx_stream_astring (is, &token, cancellable, error))
		goto fail;

	quota_root_name = g_strdup ((gchar *) token);

	tok = camel_imapx_stream_token (is, &token, &len, cancellable, error);
	switch (tok) {
		case IMAPX_TOK_ERROR:
			goto fail;
		case '(':
			break;
		default:
			g_set_error (
				error, CAMEL_IMAPX_ERROR, 1,
				"quota_response: expecting '('");
			goto fail;
	}

quota_resource:

	if (!camel_imapx_stream_atom (is, &token, &len, cancellable, error))
		goto fail;

	resource_name = g_strdup ((gchar *) token);

	if (!camel_imapx_stream_number (
		is, &resource_usage, cancellable, error))
		goto fail;

	if (!camel_imapx_stream_number (
		is, &resource_limit, cancellable, error))
		goto fail;

	info = camel_folder_quota_info_new (
		resource_name, resource_usage, resource_limit);
	g_queue_push_tail (&queue, info);

	g_free (resource_name);
	resource_name = NULL;

	tok = camel_imapx_stream_token (is, &token, &len, cancellable, error);
	switch (tok) {
		case IMAPX_TOK_ERROR:
			goto fail;
		case ')':
			break;
		default:
			camel_imapx_stream_ungettoken (is, tok, token, len);
			goto quota_resource;
	}

	/* Eat the newline. */
	if (!camel_imapx_stream_skip (is, cancellable, error))
		goto fail;

	/* String together all the CamelFolderQuotaInfo structs. */

	info = next = NULL;

	while (!g_queue_is_empty (&queue)) {
		info = g_queue_pop_tail (&queue);
		info->next = next;
		next = info;
	}

	*out_quota_root_name = quota_root_name;
	*out_quota_info = info;

	return TRUE;

fail:
	g_free (quota_root_name);
	g_free (resource_name);

	while (!g_queue_is_empty (&queue)) {
		info = g_queue_pop_head (&queue);
		camel_folder_quota_info_free (info);
	}

	return FALSE;
}

gboolean
camel_imapx_parse_quotaroot (CamelIMAPXStream *is,
                             GCancellable *cancellable,
                             gchar **out_mailbox_name,
                             gchar ***out_quota_root_names,
                             GError **error)
{
	GQueue queue = G_QUEUE_INIT;
	gint tok;
	guint len;
	guchar *token;
	gchar *mailbox_name = NULL;
	gchar **quota_root_names = NULL;
	gint ii = 0;

	g_return_val_if_fail (CAMEL_IS_IMAPX_STREAM (is), FALSE);
	g_return_val_if_fail (out_mailbox_name != NULL, FALSE);
	g_return_val_if_fail (out_quota_root_names != NULL, FALSE);

	/* quotaroot_response ::= "QUOTAROOT" SP astring *(SP astring) */

	if (!camel_imapx_stream_astring (is, &token, cancellable, error))
		goto fail;

	mailbox_name = camel_utf7_utf8 ((gchar *) token);

	while (TRUE) {
		/* Peek at the next token, and break
		 * out of the loop if we get a newline. */
		tok = camel_imapx_stream_token (
			is, &token, &len, cancellable, error);
		if (tok == '\n')
			break;
		if (tok == IMAPX_TOK_ERROR)
			goto fail;
		camel_imapx_stream_ungettoken (is, tok, token, len);

		if (!camel_imapx_stream_astring (
			is, &token, cancellable, error))
			goto fail;

		g_queue_push_tail (&queue, g_strdup ((gchar *) token));
	}

	quota_root_names = g_new0 (gchar *, queue.length + 1);
	while (!g_queue_is_empty (&queue))
		quota_root_names[ii++] = g_queue_pop_head (&queue);

	*out_mailbox_name = mailbox_name;
	*out_quota_root_names = quota_root_names;

	return TRUE;

fail:
	g_free (mailbox_name);

	while (!g_queue_is_empty (&queue))
		g_free (g_queue_pop_head (&queue));

	return FALSE;
}

/* ********************************************************************** */

/*
 * From rfc2060
 *
 * ATOM_CHAR       ::= <any CHAR except atom_specials>
 *
 * atom_specials   ::= "(" / ")" / "{" / SPACE / CTL / list_wildcards /
 *                     quoted_specials
 *
 * CHAR            ::= <any 7 - bit US - ASCII character except NUL,
 *                      0x01 - 0x7f>
 *
 * CTL             ::= <any ASCII control character and DEL,
 *                         0x00 - 0x1f, 0x7f>
 *
 * SPACE           ::= <ASCII SP, space, 0x20>
 *
 * list_wildcards  ::= "%" / "*"
 *
 * quoted_specials ::= <"> / "\"
 *
 * string          ::= quoted / literal
 *
 * literal         ::= "{" number "}" CRLF *CHAR8
 *                     ;; Number represents the number of CHAR8 octets
 *
 * quoted          ::= <"> *QUOTED_CHAR <">
 *
 * QUOTED_CHAR     ::= <any TEXT_CHAR except quoted_specials> /
 *                     "\" quoted_specials
 *
 * TEXT_CHAR       ::= <any CHAR except CR and LF>
 */

/*
 * ATOM = 1
 * SIMPLE? = 2
 * NOTID? = 4
 *
 * QSPECIAL = 8
 */

guchar imapx_specials[256] = {
/* 00 */0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 6, 0, 0, 4, 0, 0,
/* 10 */0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
/* 20 */4, 1, 0, 1, 1, 0, 1, 1, 0, 0, 2, 7, 1, 1, 1, 1,
/* 30 */1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
/* 40 */7, 7, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
/* 50 */1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 7, 0, 7, 1, 1,
/* 60 */1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
/* 70 */1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

#define list_wildcards "*%"
#define quoted_specials "\\\""
#define atom_specials "(){" list_wildcards quoted_specials /* + CTL */

/* special types for the tokeniser, come out as raw tokens */
#define token_specials "\n*()[]+"
#define notid_specials "\x20\r\n()[]+"

void
imapx_utils_init (void)
{
	static gsize imapx_utils_initialized = 0;

	if (g_once_init_enter (&imapx_utils_initialized)) {
		gint i;
		guchar v;

		for (i = 0; i < 128; i++) {
			v = 0;
			if (i >= 1 && i <= 0x7f) {
				v |= IMAPX_TYPE_CHAR;
				if (i != 0x0a && i != 0x0d) {
					v |= IMAPX_TYPE_TEXT_CHAR;
					if (i != '"' && i != '\\')
						v |= IMAPX_TYPE_QUOTED_CHAR;
				}
				if (i> 0x20 && i <0x7f && strchr (atom_specials, i) == NULL)
					v |= IMAPX_TYPE_ATOM_CHAR;
				if (strchr (token_specials, i) != NULL)
					v |= IMAPX_TYPE_TOKEN_CHAR;
				if (strchr (notid_specials, i) != NULL)
					v |= IMAPX_TYPE_NOTID_CHAR;
			}

			imapx_specials[i] = v;
		}

		create_initial_capabilities_table ();
		camel_imapx_set_debug_flags ();

		g_once_init_leave (&imapx_utils_initialized, 1);
	}
}

guchar
imapx_is_mask (const gchar *p)
{
	guchar v = 0xff;

	while (*p) {
		v &= imapx_specials[((guchar) * p) & 0xff];
		p++;
	}

	return v;
}

gchar *
imapx_path_to_physical (const gchar *prefix,
                        const gchar *vpath)
{
	GString *out = g_string_new (prefix);
	const gchar *p = vpath;
	gchar c, *res;

	g_string_append_c (out, '/');
	p = vpath;
	while ((c = *p++)) {
		if (c == '/') {
			g_string_append (out, "/" SUBFOLDER_DIR_NAME "/");
			while (*p == '/')
				p++;
		} else
			g_string_append_c (out, c);
	}

	res = out->str;
	g_string_free (out, FALSE);

	return res;
}

gchar *
imapx_concat (CamelIMAPXStore *imapx_store,
              const gchar *prefix,
              const gchar *suffix)
{
	gsize len;
	gchar dir_sep = imapx_store->dir_sep;

	if (!dir_sep)
		dir_sep = '/';

	len = strlen (prefix);
	if (len == 0 || prefix[len - 1] == dir_sep)
		return g_strdup_printf ("%s%s", prefix, suffix);
	else
		return g_strdup_printf ("%s%c%s", prefix, dir_sep, suffix);
}

static void
imapx_namespace_clear (CamelIMAPXStoreNamespace **ns)
{
	CamelIMAPXStoreNamespace *node, *next;

	node = *ns;
	while (node != NULL) {
		next = node->next;
		g_free (node->prefix);
		g_free (node);
		node = next;
	}

	*ns = NULL;
}

void
camel_imapx_namespace_list_clear (struct _CamelIMAPXNamespaceList *nsl)
{
	if (!nsl)
		return;

	imapx_namespace_clear (&nsl->personal);
	imapx_namespace_clear (&nsl->shared);
	imapx_namespace_clear (&nsl->other);

	g_free (nsl);
	nsl = NULL;
}

gchar *
imapx_get_temp_uid (void)
{
	gchar *res;

	static gint counter = 0;
	G_LOCK_DEFINE_STATIC (lock);

	G_LOCK (lock);
	res = g_strdup_printf (
		"tempuid-%lx-%d",
		(gulong) time (NULL),
		counter++);
	G_UNLOCK (lock);

	return res;
}

void
camel_imapx_destroy_job_queue_info (IMAPXJobQueueInfo *jinfo)
{
	g_hash_table_destroy (jinfo->folders);
	g_free (jinfo);
}
