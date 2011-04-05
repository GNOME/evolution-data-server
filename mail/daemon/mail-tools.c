/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with the program; if not, see <http://www.gnu.org/licenses/>
 *
 *
 * Authors:
 *		Dan Winship <danw@ximian.com>
 *		Peter Williams <peterw@ximian.com>
 *		Jeffrey Stedfast <fejj@ximian.com>
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 */

#include <config.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <ctype.h>
#include <errno.h>
#include <string.h>

#include <glib.h>
#include <glib/gstdio.h>

#include <glib/gi18n.h>

#include <libedataserver/e-data-server-util.h>
#include <libedataserver/e-account-list.h>
#include "mail-folder-cache.h"
#include "mail-session.h"
#include "mail-tools.h"
#include "e-account-utils.h"
#include "e-mail-local.h"
#include "mail-config.h"

#define d(x)

/* **************************************** */

CamelFolder *
mail_tool_get_inbox (const gchar *url, GError **error)
{
	CamelStore *store;
	CamelFolder *folder;

	store = camel_session_get_store (session, url, error);
	if (!store)
		return NULL;

	folder = camel_store_get_inbox (store, error);
	g_object_unref (store);

	return folder;
}

static gboolean
is_local_provider (CamelStore *store)
{
	CamelProvider *provider;

	g_return_val_if_fail (store != NULL, FALSE);

	provider = camel_service_get_provider (CAMEL_SERVICE (store));

	g_return_val_if_fail (provider != NULL, FALSE);

	return (provider->flags & CAMEL_PROVIDER_IS_LOCAL) != 0;
}

CamelFolder *
mail_tool_get_trash (const gchar *url,
                     gint connect,
                     GError **error)
{
	CamelStore *store;
	CamelFolder *trash;

	if (connect)
		store = camel_session_get_store (session, url, error);
	else
		store = (CamelStore *) camel_session_get_service (
			session, url, CAMEL_PROVIDER_STORE, error);

	if (!store)
		return NULL;

	if (connect ||
		(CAMEL_SERVICE (store)->status == CAMEL_SERVICE_CONNECTED ||
		is_local_provider (store)))
		trash = camel_store_get_trash (store, error);
	else
		trash = NULL;

	g_object_unref (store);

	return trash;
}

#ifndef G_OS_WIN32

static gchar *
mail_tool_get_local_movemail_path (const guchar *uri,
                                   GError **error)
{
	guchar *safe_uri, *c;
	const gchar *data_dir;
	gchar *path, *full;
	struct stat st;

	safe_uri = (guchar *)g_strdup ((const gchar *)uri);
	for (c = safe_uri; *c; c++)
		if (strchr("/:;=|%&#!*^()\\, ", *c) || !isprint((gint) *c))
			*c = '_';

	data_dir = mail_session_get_data_dir ();
	path = g_build_filename (data_dir, "spool", NULL);

	if (g_stat(path, &st) == -1 && g_mkdir_with_parents(path, 0700) == -1) {
		g_set_error (
			error, G_FILE_ERROR,
			g_file_error_from_errno (errno),
			_("Could not create spool directory '%s': %s"),
			path, g_strerror(errno));
		g_free(path);
		return NULL;
	}

	full = g_strdup_printf("%s/movemail.%s", path, safe_uri);
	g_free(path);
	g_free(safe_uri);

	return full;
}

#endif

gchar *
mail_tool_do_movemail (const gchar *source_url, GError **error)
{
#ifndef G_OS_WIN32
	gchar *dest_path;
	struct stat sb;
	CamelURL *uri;
	gboolean success;

	uri = camel_url_new(source_url, error);
	if (uri == NULL)
		return NULL;

	if (strcmp(uri->protocol, "mbox") != 0) {
		/* This is really only an internal error anyway */
		g_set_error (
			error, CAMEL_SERVICE_ERROR,
			CAMEL_SERVICE_ERROR_URL_INVALID,
			_("Trying to movemail a non-mbox source '%s'"),
			source_url);
		camel_url_free(uri);
		return NULL;
	}

	/* Set up our destination. */
	dest_path = mail_tool_get_local_movemail_path (
		(guchar *) source_url, error);
	if (dest_path == NULL)
		return NULL;

	/* Movemail from source (source_url) to dest_path */
	success = camel_movemail (uri->path, dest_path, error) != -1;
	camel_url_free(uri);

	if (g_stat (dest_path, &sb) < 0 || sb.st_size == 0) {
		g_unlink (dest_path); /* Clean up the movemail.foo file. */
		g_free (dest_path);
		return NULL;
	}

	if (!success) {
		g_free (dest_path);
		return NULL;
	}

	return dest_path;
#else
	/* Unclear yet whether camel-movemail etc makes any sense on
	 * Win32, at least it is not ported yet.
	 */
	g_warning("%s: Not implemented", __FUNCTION__);
	return NULL;
#endif
}

gchar *
mail_tool_generate_forward_subject (CamelMimeMessage *msg)
{
	const gchar *subject;
	gchar *fwd_subj;
	const gint max_subject_length = 1024;

	subject = camel_mime_message_get_subject(msg);

	if (subject && *subject) {
		/* Truncate insanely long subjects */
		if (strlen (subject) < max_subject_length) {
			fwd_subj = g_strdup_printf ("[Fwd: %s]", subject);
		} else {
			/* We can't use %.*s because it depends on the locale being C/POSIX
			   or UTF-8 to work correctly in glibc */
			/*fwd_subj = g_strdup_printf ("[Fwd: %.*s...]", max_subject_length, subject);*/
			fwd_subj = g_malloc (max_subject_length + 11);
			memcpy (fwd_subj, "[Fwd: ", 6);
			memcpy (fwd_subj + 6, subject, max_subject_length);
			memcpy (fwd_subj + 6 + max_subject_length, "...]", 5);
		}
	} else {
		const CamelInternetAddress *from;
		gchar *fromstr;

		from = camel_mime_message_get_from (msg);
		if (from) {
			fromstr = camel_address_format (CAMEL_ADDRESS (from));
			fwd_subj = g_strdup_printf ("[Fwd: %s]", fromstr);
			g_free (fromstr);
		} else
			fwd_subj = g_strdup ("[Fwd: No Subject]");
	}

	return fwd_subj;
}

struct _camel_header_raw *
mail_tool_remove_xevolution_headers (CamelMimeMessage *message)
{
	struct _camel_header_raw *scan, *list = NULL;

	for (scan = ((CamelMimePart *)message)->headers;scan;scan=scan->next)
		if (!strncmp(scan->name, "X-Evolution", 11))
			camel_header_raw_append(&list, scan->name, scan->value, scan->offset);

	for (scan=list;scan;scan=scan->next)
		camel_medium_remove_header((CamelMedium *)message, scan->name);

	return list;
}

void
mail_tool_restore_xevolution_headers (CamelMimeMessage *message,
                                      struct _camel_header_raw *xev)
{
	CamelMedium *medium;

	medium = CAMEL_MEDIUM (message);

	for (;xev;xev=xev->next)
		camel_medium_add_header (medium, xev->name, xev->value);
}

CamelMimePart *
mail_tool_make_message_attachment (CamelMimeMessage *message)
{
	CamelMimePart *part;
	const gchar *subject;
	struct _camel_header_raw *xev;
	gchar *desc;

	subject = camel_mime_message_get_subject (message);
	if (subject)
		desc = g_strdup_printf (_("Forwarded message - %s"), subject);
	else
		desc = g_strdup (_("Forwarded message"));

	/* rip off the X-Evolution headers */
	xev = mail_tool_remove_xevolution_headers (message);
	camel_header_raw_clear(&xev);

	/* remove Bcc headers */
	camel_medium_remove_header (CAMEL_MEDIUM (message), "Bcc");

	part = camel_mime_part_new ();
	camel_mime_part_set_disposition (part, "inline");
	camel_mime_part_set_description (part, desc);
	camel_medium_set_content (
		CAMEL_MEDIUM (part), CAMEL_DATA_WRAPPER (message));
	camel_mime_part_set_content_type (part, "message/rfc822");
	g_free (desc);

	return part;
}

CamelFolder *
mail_tool_uri_to_folder (const gchar *uri, guint32 flags, GError **error)
{
	CamelURL *url;
	CamelStore *store = NULL;
	CamelFolder *folder = NULL;
	gint offset = 0;
	gchar *curi = NULL;

	g_return_val_if_fail (uri != NULL, NULL);

	/* TODO: vtrash and vjunk are no longer used for these uri's */
	if (!strncmp (uri, "vtrash:", 7))
		offset = 7;
	else if (!strncmp (uri, "vjunk:", 6))
		offset = 6;
	else if (!strncmp(uri, "email:", 6)) {
		/* FIXME?: the filter:get_folder callback should do this itself? */
		curi = em_uri_to_camel(uri);
		if (uri == NULL) {
			g_set_error (
				error,
				CAMEL_ERROR, CAMEL_ERROR_GENERIC,
				_("Invalid folder: '%s'"), uri);
			return NULL;
		}
		uri = curi;
	}

	url = camel_url_new (uri + offset, error);
	if (!url) {
		g_free(curi);
		return NULL;
	}

	store = (CamelStore *) camel_session_get_service (
		session, uri + offset, CAMEL_PROVIDER_STORE, error);
	if (store) {
		const gchar *name;

		/* if we have a fragment, then the path is actually used by the store,
		   so the fragment is the path to the folder instead */
		if (url->fragment) {
			name = url->fragment;
		} else {
			if (url->path && *url->path)
				name = url->path + 1;
			else
				name = "";
		}

		if (offset) {
			if (offset == 7)
				folder = camel_store_get_trash (store, error);
			else if (offset == 6)
				folder = camel_store_get_junk (store, error);
		} else
			folder = camel_store_get_folder (store, name, flags, error);
		g_object_unref (store);
	}

	if (folder)
		mail_folder_cache_note_folder (mail_folder_cache_get_default (), folder);

	camel_url_free (url);
	g_free(curi);

	return folder;
}

/**
 * mail_tools_x_evolution_message_parse:
 * @in: GtkSelectionData->data
 * @inlen: GtkSelectionData->length
 * @uids: pointer to a gptrarray that will be filled with uids on success
 *
 * Parses the GtkSelectionData and returns a CamelFolder and a list of
 * UIDs specified by the selection.
 **/
CamelFolder *
mail_tools_x_evolution_message_parse (gchar *in, guint inlen, GPtrArray **uids)
{
	/* format: "uri\0uid1\0uid2\0uid3\0...\0uidn" */
	gchar *inptr, *inend;
	CamelFolder *folder;

	if (in == NULL)
		return NULL;

	folder = mail_tool_uri_to_folder (in, 0, NULL);

	if (!folder)
		return NULL;

	/* split the uids */
	inend = in + inlen;
	inptr = in + strlen (in) + 1;
	*uids = g_ptr_array_new ();
	while (inptr < inend) {
		gchar *start = inptr;

		while (inptr < inend && *inptr)
			inptr++;

		g_ptr_array_add (*uids, g_strndup (start, inptr - start));
		inptr++;
	}

	return folder;
}

/* FIXME: This should be a property on CamelFolder */
gchar *
mail_tools_folder_to_url (CamelFolder *folder)
{
	CamelService *service;
	CamelStore *parent_store;
	const gchar *full_name;
	CamelURL *url;
	gchar *out;

	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), NULL);

	full_name = camel_folder_get_full_name (folder);
	parent_store = camel_folder_get_parent_store (folder);
	service = CAMEL_SERVICE (parent_store);

	url = camel_url_copy (service->url);
	if (service->provider->url_flags  & CAMEL_URL_FRAGMENT_IS_PATH) {
		camel_url_set_fragment(url, full_name);
	} else {
		gchar *name = g_alloca(strlen(full_name)+2);

		sprintf(name, "/%s", full_name);
		camel_url_set_path(url, name);
	}

	out = camel_url_to_string(url, CAMEL_URL_HIDE_ALL);
	camel_url_free(url);

	return out;
}
/* Utility functions */

gchar *em_uri_to_camel(const gchar *euri)
{
	EAccountList *accounts;
	const EAccount *account;
	EAccountService *service;
	CamelProvider *provider;
	CamelURL *eurl, *curl;
	gchar *uid, *curi;

	if (strncmp(euri, "email:", 6) != 0) {
		d(printf("em uri to camel not euri '%s'\n", euri));
		return g_strdup(euri);
	}

	eurl = camel_url_new(euri, NULL);
	if (eurl == NULL)
		return g_strdup(euri);

	g_return_val_if_fail (eurl->host != NULL, g_strdup(euri));

	if (eurl->user != NULL) {
		/* Sigh, shoul'dve used mbox@local for mailboxes, not local@local */
		if (strcmp(eurl->host, "local") == 0
		    && (strcmp(eurl->user, "local") == 0 || strcmp(eurl->user, "vfolder") == 0)) {
			gchar *base;

			if (strcmp(eurl->user, "vfolder") == 0)
				curl = camel_url_new("vfolder:", NULL);
			else
				curl = camel_url_new("mbox:", NULL);

			base = g_strdup_printf("%s/mail/%s", e_get_user_data_dir(), eurl->user);
#ifdef G_OS_WIN32
			/* Turn backslashes into slashes to avoid URI encoding */
			{
				gchar *p = base;
				while ((p = strchr (p, '\\')))
					*p++ = '/';
			}
#endif
			camel_url_set_path(curl, base);
			g_free(base);
			camel_url_set_fragment(curl, eurl->path[0]=='/'?eurl->path+1:eurl->path);
			curi = camel_url_to_string(curl, 0);
			camel_url_free(curl);
			camel_url_free(eurl);

			d(printf("em uri to camel local '%s' -> '%s'\n", euri, curi));
			return curi;
		}

		uid = g_strdup_printf("%s@%s", eurl->user, eurl->host);
	} else {
		uid = g_strdup(eurl->host);
	}

	accounts = e_get_account_list ();
	account = e_account_list_find(accounts, E_ACCOUNT_FIND_UID, uid);
	g_free(uid);

	if (account == NULL) {
		camel_url_free(eurl);
		d(printf("em uri to camel no account '%s' -> '%s'\n", euri, euri));
		return g_strdup(euri);
	}

	service = account->source;
	provider = camel_provider_get (service->url, NULL);
	if (provider == NULL)
		return g_strdup (euri);

	curl = camel_url_new(service->url, NULL);
	if (provider->url_flags & CAMEL_URL_FRAGMENT_IS_PATH)
		camel_url_set_fragment(curl, eurl->path[0]=='/'?eurl->path+1:eurl->path);
	else
		camel_url_set_path(curl, eurl->path);

	curi = camel_url_to_string(curl, 0);

	camel_url_free(eurl);
	camel_url_free(curl);

	d(printf("em uri to camel '%s' -> '%s'\n", euri, curi));

	return curi;
}

/**
 * em_utils_uids_free:
 * @uids: array of uids
 *
 * Frees the array of uids pointed to by @uids back to the system.
 **/
void
em_utils_uids_free (GPtrArray *uids)
{
	gint i;

	for (i = 0; i < uids->len; i++)
		g_free (uids->pdata[i]);

	g_ptr_array_free (uids, TRUE);
}

/** em_utils_folder_is_templates:
 * @folder: folder
 * @uri: uri for this folder, if known
 *
 * Decides if @folder is a Templates folder.
 *
 * Returns %TRUE if this is a Drafts folder or %FALSE otherwise.
 **/

gboolean
em_utils_folder_is_templates (CamelFolder *folder, const gchar *uri)
{
	CamelFolder *local_templates_folder;
	CamelStore *parent_store;
	EAccountList *accounts;
	EAccount *account;
	EIterator *iter;
	gint is = FALSE;
	gchar *templates_uri;

	local_templates_folder =
		e_mail_local_get_folder (E_MAIL_FOLDER_TEMPLATES);

	if (folder == local_templates_folder)
		return TRUE;

	if (folder == NULL || uri == NULL)
		return FALSE;

	parent_store = camel_folder_get_parent_store (folder);

	accounts = e_get_account_list ();
	iter = e_list_get_iterator ((EList *)accounts);
	while (e_iterator_is_valid (iter)) {
		account = (EAccount *)e_iterator_get (iter);

		if (account->templates_folder_uri) {
			templates_uri = em_uri_to_camel (account->templates_folder_uri);
			if (camel_store_folder_uri_equal (parent_store, templates_uri, uri)) {
				g_free (templates_uri);
				is = TRUE;
				break;
			}
			g_free (templates_uri);
		}

		e_iterator_next (iter);
	}

	g_object_unref (iter);

	return is;
}

/**
 * em_utils_folder_is_drafts:
 * @folder: folder
 * @uri: uri for this folder, if known
 *
 * Decides if @folder is a Drafts folder.
 *
 * Returns %TRUE if this is a Drafts folder or %FALSE otherwise.
 **/
gboolean
em_utils_folder_is_drafts(CamelFolder *folder, const gchar *uri)
{
	CamelFolder *local_drafts_folder;
	CamelStore *parent_store;
	EAccountList *accounts;
	EAccount *account;
	EIterator *iter;
	gint is = FALSE;
	gchar *drafts_uri;

	local_drafts_folder =
		e_mail_local_get_folder (E_MAIL_FOLDER_DRAFTS);

	if (folder == local_drafts_folder)
		return TRUE;

	if (folder == NULL || uri == NULL)
		return FALSE;

	parent_store = camel_folder_get_parent_store (folder);

	accounts = e_get_account_list ();
	iter = e_list_get_iterator((EList *)accounts);
	while (e_iterator_is_valid(iter)) {
		account = (EAccount *)e_iterator_get(iter);

		if (account->drafts_folder_uri) {
			drafts_uri = em_uri_to_camel (account->drafts_folder_uri);
			if (camel_store_folder_uri_equal (parent_store, drafts_uri, uri)) {
				g_free (drafts_uri);
				is = TRUE;
				break;
			}
			g_free (drafts_uri);
		}

		e_iterator_next(iter);
	}

	g_object_unref(iter);

	return is;
}

/**
 * em_utils_folder_is_sent:
 * @folder: folder
 * @uri: uri for this folder, if known
 *
 * Decides if @folder is a Sent folder
 *
 * Returns %TRUE if this is a Sent folder or %FALSE otherwise.
 **/
gboolean
em_utils_folder_is_sent(CamelFolder *folder, const gchar *uri)
{
	CamelFolder *local_sent_folder;
	CamelStore *parent_store;
	EAccountList *accounts;
	EAccount *account;
	EIterator *iter;
	gint is = FALSE;
	gchar *sent_uri;

	local_sent_folder = e_mail_local_get_folder (E_MAIL_FOLDER_SENT);

	if (folder == local_sent_folder)
		return TRUE;

	if (folder == NULL || uri == NULL)
		return FALSE;

	parent_store = camel_folder_get_parent_store (folder);

	accounts = e_get_account_list ();
	iter = e_list_get_iterator((EList *)accounts);
	while (e_iterator_is_valid(iter)) {
		account = (EAccount *)e_iterator_get(iter);

		if (account->sent_folder_uri) {
			sent_uri = em_uri_to_camel (account->sent_folder_uri);
			if (camel_store_folder_uri_equal (parent_store, sent_uri, uri)) {
				g_free (sent_uri);
				is = TRUE;
				break;
			}
			g_free (sent_uri);
		}

		e_iterator_next(iter);
	}

	g_object_unref(iter);

	return is;
}

/**
 * em_utils_folder_is_outbox:
 * @folder: folder
 * @uri: uri for this folder, if known
 *
 * Decides if @folder is an Outbox folder
 *
 * Returns %TRUE if this is an Outbox folder or %FALSE otherwise.
 **/
gboolean
em_utils_folder_is_outbox(CamelFolder *folder, const gchar *uri)
{
	CamelFolder *local_outbox_folder;
	const gchar *local_outbox_folder_uri;

	local_outbox_folder =
		e_mail_local_get_folder (E_MAIL_FOLDER_OUTBOX);
	local_outbox_folder_uri =
		e_mail_local_get_folder_uri (E_MAIL_FOLDER_OUTBOX);

	if (folder == local_outbox_folder)
		return TRUE;

	if (uri == NULL)
		return FALSE;

	return camel_store_folder_uri_equal (
		camel_folder_get_parent_store (local_outbox_folder),
		local_outbox_folder_uri, uri);
}

static EAccount *
guess_account_from_folder (CamelFolder *folder)
{
	CamelService *service;
	CamelStore *parent_store;
	EAccount *account;
	gchar *source_url;

	parent_store = camel_folder_get_parent_store (folder);
	service = CAMEL_SERVICE (parent_store);

	source_url = camel_url_to_string (service->url, CAMEL_URL_HIDE_ALL);
	account = mail_config_get_account_by_source_url (source_url);
	g_free (source_url);

	return account;
}

static EAccount *
guess_account_from_message (CamelMimeMessage *message)
{
	const gchar *source_url;

	source_url = camel_mime_message_get_source (message);
	if (source_url == NULL)
		return NULL;

	return mail_config_get_account_by_source_url (source_url);
}

GHashTable *
em_utils_generate_account_hash (void)
{
	GHashTable *account_hash;
	EAccount *account, *def;
	EAccountList *accounts;
	EIterator *iter;

	accounts = e_get_account_list ();
	account_hash = g_hash_table_new (camel_strcase_hash, camel_strcase_equal);

	def = e_get_default_account ();

	iter = e_list_get_iterator ((EList *) accounts);
	while (e_iterator_is_valid (iter)) {
		account = (EAccount *) e_iterator_get (iter);

		if (account->id->address) {
			EAccount *acnt;

			/* Accounts with identical email addresses that are enabled
			 * take precedence over the accounts that aren't. If all
			 * accounts with matching email addresses are disabled, then
			 * the first one in the list takes precedence. The default
			 * account always takes precedence no matter what.
			 */
			acnt = g_hash_table_lookup (account_hash, account->id->address);
			if (acnt && acnt != def && !acnt->enabled && account->enabled) {
				g_hash_table_remove (account_hash, acnt->id->address);
				acnt = NULL;
			}

			if (!acnt)
				g_hash_table_insert (account_hash, (gchar *) account->id->address, (gpointer) account);
		}

		e_iterator_next (iter);
	}

	g_object_unref (iter);

	/* The default account has to be there if none of the enabled accounts are present */
	if (g_hash_table_size (account_hash) == 0 && def && def->id->address)
		g_hash_table_insert (account_hash, (gchar *) def->id->address, (gpointer) def);

	return account_hash;
}

EAccount *
em_utils_guess_account (CamelMimeMessage *message,
                        CamelFolder *folder)
{
	EAccount *account = NULL;

	g_return_val_if_fail (CAMEL_IS_MIME_MESSAGE (message), NULL);

	if (folder != NULL)
		g_return_val_if_fail (CAMEL_IS_FOLDER (folder), NULL);

	/* check for newsgroup header */
	if (folder != NULL
	    && camel_medium_get_header (CAMEL_MEDIUM (message), "Newsgroups"))
		account = guess_account_from_folder (folder);

	/* check for source folder */
	if (account == NULL && folder != NULL)
		account = guess_account_from_folder (folder);

	/* then message source */
	if (account == NULL)
		account = guess_account_from_message (message);

	return account;
}

EAccount *
em_utils_guess_account_with_recipients (CamelMimeMessage *message,
                                        CamelFolder *folder)
{
	EAccount *account = NULL;
	EAccountList *account_list;
	GHashTable *recipients;
	EIterator *iter;
	CamelInternetAddress *addr;
	const gchar *type;
	const gchar *key;

	/* This policy is subject to debate and tweaking,
	 * but please also document the rational here. */

	g_return_val_if_fail (CAMEL_IS_MIME_MESSAGE (message), NULL);

	/* Build a set of email addresses in which to test for membership.
	 * Only the keys matter here; the values just need to be non-NULL. */
	recipients = g_hash_table_new (g_str_hash, g_str_equal);

	type = CAMEL_RECIPIENT_TYPE_TO;
	addr = camel_mime_message_get_recipients (message, type);
	if (addr != NULL) {
		gint index = 0;

		while (camel_internet_address_get (addr, index++, NULL, &key))
			g_hash_table_insert (
				recipients, (gpointer) key,
				GINT_TO_POINTER (1));
	}

	type = CAMEL_RECIPIENT_TYPE_CC;
	addr = camel_mime_message_get_recipients (message, type);
	if (addr != NULL) {
		gint index = 0;

		while (camel_internet_address_get (addr, index++, NULL, &key))
			g_hash_table_insert (
				recipients, (gpointer) key,
				GINT_TO_POINTER (1));
	}

	/* First Preference: We were given a folder that maps to an
	 * enabled account, and that account's email address appears
	 * in the list of To: or Cc: recipients. */

	if (folder != NULL)
		account = guess_account_from_folder (folder);

	if (account == NULL || !account->enabled)
		goto second_preference;

	if ((key = account->id->address) == NULL)
		goto second_preference;

	if (g_hash_table_lookup (recipients, key) != NULL)
		goto exit;

second_preference:

	/* Second Preference: Choose any enabled account whose email
	 * address appears in the list to To: or Cc: recipients. */

	account_list = e_get_account_list ();
	iter = e_list_get_iterator (E_LIST (account_list));
	while (e_iterator_is_valid (iter)) {
		account = (EAccount *) e_iterator_get (iter);
		e_iterator_next (iter);

		if (account == NULL || !account->enabled)
			continue;

		if ((key = account->id->address) == NULL)
			continue;

		if (g_hash_table_lookup (recipients, key) != NULL) {
			g_object_unref (iter);
			goto exit;
		}
	}
	g_object_unref (iter);

	/* Last Preference: Defer to em_utils_guess_account(). */
	account = em_utils_guess_account (message, folder);

exit:
	g_hash_table_destroy (recipients);

	return account;
}


