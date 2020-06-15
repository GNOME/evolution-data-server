/*
 * camel-imapx-search.c
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
 */

#include "evolution-data-server-config.h"

#include "camel-imapx-search.h"

#include <string.h>
#include <camel/camel.h>
#include <camel/camel-search-private.h>

#include "camel-imapx-folder.h"

struct _CamelIMAPXSearchPrivate {
	GWeakRef imapx_store;
	gint *local_data_search; /* not NULL, if testing whether all used headers are all locally available */

	GHashTable *cached_results; /* gchar * (search description) ~> GHashTable { gchar * (uid {from string pool}) ~> NULL } */
	GCancellable *cancellable; /* not referenced */
	GError **error; /* not referenced */
};

enum {
	PROP_0,
	PROP_STORE
};

G_DEFINE_TYPE_WITH_PRIVATE (
	CamelIMAPXSearch,
	camel_imapx_search,
	CAMEL_TYPE_FOLDER_SEARCH)

static void
imapx_search_set_property (GObject *object,
                           guint property_id,
                           const GValue *value,
                           GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_STORE:
			camel_imapx_search_set_store (
				CAMEL_IMAPX_SEARCH (object),
				g_value_get_object (value));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
imapx_search_get_property (GObject *object,
                           guint property_id,
                           GValue *value,
                           GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_STORE:
			g_value_take_object (
				value,
				camel_imapx_search_ref_store (
				CAMEL_IMAPX_SEARCH (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
imapx_search_dispose (GObject *object)
{
	CamelIMAPXSearchPrivate *priv;

	priv = CAMEL_IMAPX_SEARCH (object)->priv;

	g_weak_ref_set (&priv->imapx_store, NULL);

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (camel_imapx_search_parent_class)->dispose (object);
}

static void
imapx_search_finalize (GObject *object)
{
	CamelIMAPXSearchPrivate *priv;

	priv = CAMEL_IMAPX_SEARCH (object)->priv;

	g_weak_ref_clear (&priv->imapx_store);
	g_hash_table_destroy (priv->cached_results);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (camel_imapx_search_parent_class)->finalize (object);
}

static CamelSExpResult *
imapx_search_result_match_all (CamelSExp *sexp,
                               CamelFolderSearch *search)
{
	CamelSExpResult *result;

	g_return_val_if_fail (search != NULL, NULL);

	if (camel_folder_search_get_current_message_info (search)) {
		result = camel_sexp_result_new (sexp, CAMEL_SEXP_RES_BOOL);
		result->value.boolean = TRUE;
	} else {
		GPtrArray *summary;
		gint ii;

		summary = camel_folder_search_get_summary (search);

		result = camel_sexp_result_new (sexp, CAMEL_SEXP_RES_ARRAY_PTR);
		result->value.ptrarray = g_ptr_array_new ();

		for (ii = 0; summary && ii < summary->len; ii++) {
			g_ptr_array_add (
				result->value.ptrarray,
				(gpointer) summary->pdata[ii]);
		}
	}

	return result;
}

static CamelSExpResult *
imapx_search_result_match_none (CamelSExp *sexp,
                                CamelFolderSearch *search)
{
	CamelSExpResult *result;

	g_return_val_if_fail (search != NULL, NULL);

	if (camel_folder_search_get_current_message_info (search)) {
		result = camel_sexp_result_new (sexp, CAMEL_SEXP_RES_BOOL);
		result->value.boolean = FALSE;
	} else {
		result = camel_sexp_result_new (sexp, CAMEL_SEXP_RES_ARRAY_PTR);
		result->value.ptrarray = g_ptr_array_new ();
	}

	return result;
}

static gchar *
imapx_search_describe_criteria (const GString *criteria_prefix,
				const gchar *search_key,
				const GPtrArray *words)
{
	GString *desc;

	desc = g_string_sized_new (64);

	if (criteria_prefix && criteria_prefix->len)
		g_string_append (desc, criteria_prefix->str);

	g_string_append_c (desc, '\n');

	if (search_key && *search_key)
		g_string_append (desc, search_key);

	g_string_append_c (desc, '\n');

	if (words && words->len) {
		guint ii;

		for (ii = 0; ii < words->len; ii++) {
			const gchar *word = words->pdata[ii];

			if (word) {
				g_string_append (desc, word);
				g_string_append_c (desc, '\n');
			}
		}
	}

	g_string_append_c (desc, '\n');

	return g_string_free (desc, FALSE);
}

static CamelSExpResult *
imapx_search_process_criteria (CamelSExp *sexp,
                               CamelFolderSearch *search,
                               CamelIMAPXStore *imapx_store,
                               const GString *criteria_prefix,
			       const gchar *search_key,
			       const GPtrArray *words,
                               const gchar *from_function)
{
	CamelSExpResult *result;
	CamelIMAPXSearch *imapx_search = CAMEL_IMAPX_SEARCH (search);
	CamelIMAPXMailbox *mailbox;
	CamelMessageInfo *info;
	GHashTable *cached_results;
	gchar *criteria_desc;
	GPtrArray *uids = NULL;
	GError *local_error = NULL;

	criteria_desc = imapx_search_describe_criteria (criteria_prefix, search_key, words);
	cached_results = g_hash_table_lookup (imapx_search->priv->cached_results, criteria_desc);

	if (!cached_results) {
		mailbox = camel_imapx_folder_list_mailbox (
			CAMEL_IMAPX_FOLDER (camel_folder_search_get_folder (search)), imapx_search->priv->cancellable, &local_error);

		/* Sanity check. */
		g_return_val_if_fail (
			((mailbox != NULL) && (local_error == NULL)) ||
			((mailbox == NULL) && (local_error != NULL)), NULL);

		if (mailbox != NULL) {
			CamelIMAPXConnManager *conn_man;

			conn_man = camel_imapx_store_get_conn_manager (imapx_store);
			uids = camel_imapx_conn_manager_uid_search_sync (conn_man, mailbox, criteria_prefix ? criteria_prefix->str : "", search_key,
				words ? (const gchar * const *) words->pdata : NULL, imapx_search->priv->cancellable, &local_error);
			g_object_unref (mailbox);
		}

		/* Sanity check. */
		g_return_val_if_fail (
			((uids != NULL) && (local_error == NULL)) ||
			((uids == NULL) && (local_error != NULL)), NULL);

		if (local_error != NULL) {
			g_propagate_error (imapx_search->priv->error, local_error);

			/* Make like we've got an empty result */
			uids = g_ptr_array_new ();
		}
	}

	if (!cached_results) {
		cached_results = g_hash_table_new_full (g_str_hash, g_str_equal, (GDestroyNotify) camel_pstring_free, NULL);

		if (uids) {
			guint ii;

			for (ii = 0; ii < uids->len; ii++) {
				g_hash_table_insert (cached_results, (gpointer) camel_pstring_strdup (uids->pdata[ii]), NULL);
			}
		}

		g_hash_table_insert (imapx_search->priv->cached_results, criteria_desc, cached_results);
	} else {
		g_free (criteria_desc);
	}

	info = camel_folder_search_get_current_message_info (search);

	if (info) {
		result = camel_sexp_result_new (sexp, CAMEL_SEXP_RES_BOOL);
		result->value.boolean = g_hash_table_contains (cached_results, camel_message_info_get_uid (info));
	} else {
		if (!uids) {
			GHashTableIter iter;
			gpointer key;

			uids = g_ptr_array_sized_new (g_hash_table_size (cached_results));

			g_hash_table_iter_init (&iter, cached_results);

			while (g_hash_table_iter_next (&iter, &key, NULL)) {
				g_ptr_array_add (uids, (gpointer) camel_pstring_strdup (key));
			}
		}

		result = camel_sexp_result_new (sexp, CAMEL_SEXP_RES_ARRAY_PTR);
		result->value.ptrarray = g_ptr_array_ref (uids);
	}

	if (uids)
		g_ptr_array_unref (uids);

	return result;
}

static GPtrArray *
imapx_search_gather_words (CamelSExpResult **argv,
			   gint from_index,
			   gint argc)
{
	GPtrArray *ptrs;
	GHashTable *words_hash;
	GHashTableIter iter;
	gpointer key, value;
	gint ii, jj;

	g_return_val_if_fail (argv != 0, NULL);

	words_hash = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

	for (ii = from_index; ii < argc; ii++) {
		struct _camel_search_words *words;

		if (argv[ii]->type != CAMEL_SEXP_RES_STRING)
			continue;

		/* Handle multiple search words within a single term. */
		words = camel_search_words_split ((const guchar *) argv[ii]->value.string);

		for (jj = 0; jj < words->len; jj++) {
			const gchar *word = words->words[jj]->word;

			g_hash_table_insert (words_hash, g_strdup (word), NULL);
		}

		camel_search_words_free (words);
	}

	ptrs = g_ptr_array_new_full (g_hash_table_size (words_hash), g_free);

	g_hash_table_iter_init (&iter, words_hash);
	while (g_hash_table_iter_next (&iter, &key, &value)) {
		g_ptr_array_add (ptrs, g_strdup (key));
	}

	if (ptrs->len == 0) {
		g_ptr_array_free (ptrs, TRUE);
		ptrs = NULL;
	} else {
		g_ptr_array_add (ptrs, NULL);
	}

	g_hash_table_destroy (words_hash);

	return ptrs;
}

static CamelSExpResult *
imapx_search_body_contains (CamelSExp *sexp,
                            gint argc,
                            CamelSExpResult **argv,
                            CamelFolderSearch *search)
{
	CamelIMAPXSearch *imapx_search = CAMEL_IMAPX_SEARCH (search);
	CamelIMAPXStore *imapx_store;
	CamelSExpResult *result;
	GPtrArray *words;
	gboolean is_gmail;

	/* Always do body-search server-side */
	if (imapx_search->priv->local_data_search) {
		*imapx_search->priv->local_data_search = -1;
		return imapx_search_result_match_none (sexp, search);
	}

	/* Match everything if argv = [""] */
	if (argc == 1 && argv[0]->value.string[0] == '\0')
		return imapx_search_result_match_all (sexp, search);

	/* Match nothing if empty argv or empty summary. */
	if (argc == 0 || camel_folder_search_get_summary_empty (search))
		return imapx_search_result_match_none (sexp, search);

	imapx_store = camel_imapx_search_ref_store (CAMEL_IMAPX_SEARCH (search));

	/* This will be NULL if we're offline. Search from cache. */
	if (imapx_store == NULL) {
		/* Chain up to parent's method. */
		return CAMEL_FOLDER_SEARCH_CLASS (camel_imapx_search_parent_class)->
			body_contains (sexp, argc, argv, search);
	}

	/* Build the IMAP search criteria. */

	words = imapx_search_gather_words (argv, 0, argc);

	result = imapx_search_process_criteria (sexp, search, imapx_store, NULL, "BODY", words, G_STRFUNC);

	is_gmail = camel_imapx_store_is_gmail_server (imapx_store);

	g_ptr_array_free (words, TRUE);
	g_object_unref (imapx_store);

	if (is_gmail && result && (result->type == CAMEL_SEXP_RES_ARRAY_PTR || (result->type == CAMEL_SEXP_RES_BOOL && !result->value.boolean))) {
		/* Gmail returns BODY matches on whole words only, which is not it should be,
		   thus try also locally cached messages to provide any better results. */
		gboolean was_only_cached_messages;
		CamelSExpResult *cached_result;

		was_only_cached_messages = camel_folder_search_get_only_cached_messages (search);
		camel_folder_search_set_only_cached_messages (search, TRUE);

		cached_result = CAMEL_FOLDER_SEARCH_CLASS (camel_imapx_search_parent_class)->body_contains (sexp, argc, argv, search);

		camel_folder_search_set_only_cached_messages (search, was_only_cached_messages);

		if (cached_result && cached_result->type == result->type) {
			if (result->type == CAMEL_SEXP_RES_BOOL) {
				result->value.boolean = cached_result->value.boolean;
			} else {
				/* Merge the two UID arrays */
				GHashTable *merge;
				GHashTableIter iter;
				GPtrArray *array;
				gpointer key;
				guint ii;

				/* UID-s are strings from the string pool, thus can be compared directly */
				merge = g_hash_table_new (g_direct_hash, g_direct_equal);

				array = result->value.ptrarray;
				for (ii = 0; array && ii < array->len; ii++) {
					gpointer uid = g_ptr_array_index (array, ii);

					if (uid)
						g_hash_table_insert (merge, uid, NULL);
				}

				array = cached_result->value.ptrarray;
				for (ii = 0; array && ii < array->len; ii++) {
					gpointer uid = g_ptr_array_index (array, ii);

					if (uid)
						g_hash_table_insert (merge, uid, NULL);
				}

				array = g_ptr_array_new_full (g_hash_table_size (merge), (GDestroyNotify) camel_pstring_free);

				g_hash_table_iter_init (&iter, merge);
				while (g_hash_table_iter_next (&iter, &key, NULL)) {
					g_ptr_array_add (array, (gpointer) camel_pstring_strdup (key));
				}

				g_hash_table_destroy (merge);

				g_ptr_array_unref (result->value.ptrarray);

				result->value.ptrarray = array;
			}
		}

		camel_sexp_result_free (sexp, cached_result);
	}

	return result;
}

static gboolean
imapx_search_is_header_from_summary (const gchar *header_name)
{
	return  g_ascii_strcasecmp (header_name, "From") == 0 ||
		g_ascii_strcasecmp (header_name, "To") == 0 ||
		g_ascii_strcasecmp (header_name, "CC") == 0 ||
		g_ascii_strcasecmp (header_name, "Subject") == 0;
}

static CamelSExpResult *
imapx_search_header_contains (CamelSExp *sexp,
                              gint argc,
                              CamelSExpResult **argv,
                              CamelFolderSearch *search)
{
	CamelIMAPXSearch *imapx_search = CAMEL_IMAPX_SEARCH (search);
	CamelIMAPXStore *imapx_store;
	CamelSExpResult *result;
	const gchar *headername, *command = NULL;
	gchar *search_key = NULL;
	GPtrArray *words;

	/* Match nothing if empty argv or empty summary. */
	if (argc <= 1 ||
	    argv[0]->type != CAMEL_SEXP_RES_STRING ||
	    camel_folder_search_get_summary_empty (search))
		return imapx_search_result_match_none (sexp, search);

	headername = argv[0]->value.string;

	if (imapx_search_is_header_from_summary (headername)) {
		if (imapx_search->priv->local_data_search) {
			if (*imapx_search->priv->local_data_search >= 0)
				*imapx_search->priv->local_data_search = (*imapx_search->priv->local_data_search) + 1;
			return imapx_search_result_match_all (sexp, search);
		}

		/* Chain up to parent's method. */
		return CAMEL_FOLDER_SEARCH_CLASS (camel_imapx_search_parent_class)->
			header_contains (sexp, argc, argv, search);
	} else if (imapx_search->priv->local_data_search) {
		*imapx_search->priv->local_data_search = -1;
		return imapx_search_result_match_none (sexp, search);
	}

	imapx_store = camel_imapx_search_ref_store (CAMEL_IMAPX_SEARCH (search));

	/* This will be NULL if we're offline. Search from cache. */
	if (imapx_store == NULL) {
		/* Chain up to parent's method. */
		return CAMEL_FOLDER_SEARCH_CLASS (camel_imapx_search_parent_class)->
			header_contains (sexp, argc, argv, search);
	}

	/* Build the IMAP search criteria. */

	if (g_ascii_strcasecmp (headername, "From") == 0)
		command = "FROM";
	else if (g_ascii_strcasecmp (headername, "To") == 0)
		command = "TO";
	else if (g_ascii_strcasecmp (headername, "CC") == 0)
		command = "CC";
	else if (g_ascii_strcasecmp (headername, "Bcc") == 0)
		command = "BCC";
	else if (g_ascii_strcasecmp (headername, "Subject") == 0)
		command = "SUBJECT";

	words = imapx_search_gather_words (argv, 1, argc);

	if (!command)
		search_key = g_strdup_printf ("HEADER \"%s\"", headername);

	result = imapx_search_process_criteria (sexp, search, imapx_store, NULL, command ? command : search_key, words, G_STRFUNC);

	g_ptr_array_free (words, TRUE);
	g_object_unref (imapx_store);
	g_free (search_key);

	return result;
}

static CamelSExpResult *
imapx_search_header_exists (CamelSExp *sexp,
                            gint argc,
                            CamelSExpResult **argv,
                            CamelFolderSearch *search)
{
	CamelIMAPXSearch *imapx_search = CAMEL_IMAPX_SEARCH (search);
	CamelIMAPXStore *imapx_store;
	CamelSExpResult *result;
	GString *criteria;
	gint ii;

	/* Match nothing if empty argv or empty summary. */
	if (argc == 0 || camel_folder_search_get_summary_empty (search))
		return imapx_search_result_match_none (sexp, search);

	/* Check if asking for locally stored headers only */
	for (ii = 0; ii < argc; ii++) {
		if (argv[ii]->type != CAMEL_SEXP_RES_STRING)
			continue;

		if (!imapx_search_is_header_from_summary (argv[ii]->value.string))
			break;
	}

	/* All headers are from summary */
	if (ii == argc) {
		if (imapx_search->priv->local_data_search) {
			if (*imapx_search->priv->local_data_search >= 0)
				*imapx_search->priv->local_data_search = (*imapx_search->priv->local_data_search) + 1;

			return imapx_search_result_match_all (sexp, search);
		}

		/* Chain up to parent's method. */
		return CAMEL_FOLDER_SEARCH_CLASS (camel_imapx_search_parent_class)->
			header_exists (sexp, argc, argv, search);
	} else if (imapx_search->priv->local_data_search) {
		*imapx_search->priv->local_data_search = -1;
		return imapx_search_result_match_none (sexp, search);
	}

	imapx_store = camel_imapx_search_ref_store (CAMEL_IMAPX_SEARCH (search));

	/* This will be NULL if we're offline. Search from cache. */
	if (imapx_store == NULL) {
		/* Chain up to parent's method. */
		return CAMEL_FOLDER_SEARCH_CLASS (camel_imapx_search_parent_class)->
			header_exists (sexp, argc, argv, search);
	}

	/* Build the IMAP search criteria. */

	criteria = g_string_sized_new (128);

	for (ii = 0; ii < argc; ii++) {
		const gchar *headername;

		if (argv[ii]->type != CAMEL_SEXP_RES_STRING)
			continue;

		headername = argv[ii]->value.string;

		if (criteria->len > 0)
			g_string_append_c (criteria, ' ');

		g_string_append_printf (criteria, "HEADER \"%s\" \"\"", headername);
	}

	result = imapx_search_process_criteria (sexp, search, imapx_store, criteria, NULL, NULL, G_STRFUNC);

	g_object_unref (imapx_store);

	return result;
}

static void
camel_imapx_search_class_init (CamelIMAPXSearchClass *class)
{
	GObjectClass *object_class;
	CamelFolderSearchClass *search_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = imapx_search_set_property;
	object_class->get_property = imapx_search_get_property;
	object_class->dispose = imapx_search_dispose;
	object_class->finalize = imapx_search_finalize;

	search_class = CAMEL_FOLDER_SEARCH_CLASS (class);
	search_class->body_contains = imapx_search_body_contains;
	search_class->header_contains = imapx_search_header_contains;
	search_class->header_exists = imapx_search_header_exists;

	g_object_class_install_property (
		object_class,
		PROP_STORE,
		g_param_spec_object (
			"store",
			"IMAPX Store",
			"IMAPX Store for server-side searches",
			CAMEL_TYPE_IMAPX_STORE,
			G_PARAM_READWRITE |
			G_PARAM_EXPLICIT_NOTIFY |
			G_PARAM_STATIC_STRINGS));
}

static void
camel_imapx_search_init (CamelIMAPXSearch *search)
{
	search->priv = camel_imapx_search_get_instance_private (search);
	search->priv->local_data_search = NULL;
	search->priv->cached_results = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, (GDestroyNotify) g_hash_table_destroy);

	g_weak_ref_init (&search->priv->imapx_store, NULL);
}

/**
 * camel_imapx_search_new:
 * @imapx_store: a #CamelIMAPXStore to which the search belongs
 *
 * Returns a new #CamelIMAPXSearch instance.
 *
 * Returns: a new #CamelIMAPXSearch
 *
 * Since: 3.8
 **/
CamelFolderSearch *
camel_imapx_search_new (CamelIMAPXStore *imapx_store)
{
	g_return_val_if_fail (CAMEL_IS_IMAPX_STORE (imapx_store), NULL);

	return g_object_new (
		CAMEL_TYPE_IMAPX_SEARCH,
		"store", imapx_store,
		NULL);
}

/**
 * camel_imapx_search_ref_store:
 * @search: a #CamelIMAPXSearch
 *
 * Returns a #CamelIMAPXStore to use for server-side searches,
 * or %NULL when the store is offline or when the current folder
 * is synchronized for offline use.
 *
 * The returned #CamelIMAPXStore is referenced for thread-safety and
 * must be unreferenced with g_object_unref() when finished with it.
 *
 * Returns: a #CamelIMAPXStore, or %NULL
 *
 * Since: 3.8
 **/
CamelIMAPXStore *
camel_imapx_search_ref_store (CamelIMAPXSearch *search)
{
	CamelIMAPXStore *imapx_store;

	g_return_val_if_fail (CAMEL_IS_IMAPX_SEARCH (search), NULL);

	imapx_store = g_weak_ref_get (&search->priv->imapx_store);

	if (imapx_store) {
		if (!camel_offline_store_get_online (CAMEL_OFFLINE_STORE (imapx_store))) {
			g_clear_object (&imapx_store);
		} else {
			CamelFolder *folder;

			folder = camel_folder_search_get_folder (CAMEL_FOLDER_SEARCH (search));

			if (CAMEL_IS_OFFLINE_FOLDER (folder) && camel_offline_folder_can_downsync (CAMEL_OFFLINE_FOLDER (folder))) {
				CamelSettings *settings;

				settings = camel_service_ref_settings (CAMEL_SERVICE (imapx_store));

				/* Downloads everything for the offline usage, thus search in the local cache only */
				if (settings && !camel_offline_settings_get_limit_by_age (CAMEL_OFFLINE_SETTINGS (settings)))
					g_clear_object (&imapx_store);

				g_clear_object (&settings);
			}
		}
	}

	return imapx_store;
}

/**
 * camel_imapx_search_set_store:
 * @search: a #CamelIMAPXSearch
 * @imapx_store: a #CamelIMAPXStore, or %NULL
 *
 * Sets a #CamelIMAPXStore to use for server-side searches. Generally
 * this is set for the duration of a single search when online, and then
 * reset to %NULL.
 *
 * Since: 3.8
 **/
void
camel_imapx_search_set_store (CamelIMAPXSearch *search,
			      CamelIMAPXStore *imapx_store)
{
	g_return_if_fail (CAMEL_IS_IMAPX_SEARCH (search));

	if (imapx_store != NULL)
		g_return_if_fail (CAMEL_IS_IMAPX_STORE (imapx_store));

	g_weak_ref_set (&search->priv->imapx_store, imapx_store);

	g_object_notify (G_OBJECT (search), "store");
}

void
camel_imapx_search_clear_cached_results (CamelIMAPXSearch *search)
{
	g_return_if_fail (CAMEL_IS_IMAPX_SEARCH (search));

	g_hash_table_remove_all (search->priv->cached_results);
}

/**
 * camel_imapx_search_set_cancellable_and_error:
 * @search: a #CamelIMAPXSearch
 * @cancellable: a #GCancellable, or %NULL
 * @error: a #GError, or %NULL
 *
 * Sets @cancellable and @error to use for server-side searches. This way
 * the search can return accurate errors and be eventually cancelled by
 * a user.
 *
 * Note: The caller is responsible to keep alive both @cancellable and @error
 * for the whole run of the search and reset them both to NULL after
 * the search is finished.
 *
 * Since: 3.16
 **/
void
camel_imapx_search_set_cancellable_and_error (CamelIMAPXSearch *search,
					      GCancellable *cancellable,
					      GError **error)
{
	g_return_if_fail (CAMEL_IS_IMAPX_SEARCH (search));

	if (cancellable)
		g_return_if_fail (G_IS_CANCELLABLE (cancellable));

	search->priv->cancellable = cancellable;
	search->priv->error = error;
}
