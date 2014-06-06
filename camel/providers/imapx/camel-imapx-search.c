/*
 * camel-imapx-search.c
 *
 * This library is free software you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "camel-imapx-search.h"

#include <camel/camel.h>
#include <camel/camel-search-private.h>

#include "camel-imapx-folder.h"

#define CAMEL_IMAPX_SEARCH_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), CAMEL_TYPE_IMAPX_SEARCH, CamelIMAPXSearchPrivate))

struct _CamelIMAPXSearchPrivate {
	GWeakRef imapx_store;
	gint *local_data_search; /* not NULL, if testing whether all used headers are all locally available */

	GCancellable *cancellable; /* not referenced */
	GError **error; /* not referenced */
};

enum {
	PROP_0,
	PROP_STORE
};

G_DEFINE_TYPE (
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

	priv = CAMEL_IMAPX_SEARCH_GET_PRIVATE (object);

	g_weak_ref_set (&priv->imapx_store, NULL);

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (camel_imapx_search_parent_class)->dispose (object);
}

static void
imapx_search_finalize (GObject *object)
{
	CamelIMAPXSearchPrivate *priv;

	priv = CAMEL_IMAPX_SEARCH_GET_PRIVATE (object);

	g_weak_ref_clear (&priv->imapx_store);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (camel_imapx_search_parent_class)->finalize (object);
}

static CamelSExpResult *
imapx_search_result_match_all (CamelSExp *sexp,
                               CamelFolderSearch *search)
{
	CamelSExpResult *result;

	g_return_val_if_fail (search != NULL, NULL);

	if (search->current != NULL) {
		result = camel_sexp_result_new (sexp, CAMEL_SEXP_RES_BOOL);
		result->value.boolean = TRUE;
	} else {
		gint ii;

		result = camel_sexp_result_new (sexp, CAMEL_SEXP_RES_ARRAY_PTR);
		result->value.ptrarray = g_ptr_array_new ();

		for (ii = 0; ii < search->summary->len; ii++)
			g_ptr_array_add (
				result->value.ptrarray,
				(gpointer) search->summary->pdata[ii]);
	}

	return result;
}

static CamelSExpResult *
imapx_search_result_match_none (CamelSExp *sexp,
                                CamelFolderSearch *search)
{
	CamelSExpResult *result;

	g_return_val_if_fail (search != NULL, NULL);

	if (search->current != NULL) {
		result = camel_sexp_result_new (sexp, CAMEL_SEXP_RES_BOOL);
		result->value.boolean = FALSE;
	} else {
		result = camel_sexp_result_new (sexp, CAMEL_SEXP_RES_ARRAY_PTR);
		result->value.ptrarray = g_ptr_array_new ();
	}

	return result;
}

static CamelSExpResult *
imapx_search_process_criteria (CamelSExp *sexp,
                               CamelFolderSearch *search,
                               CamelIMAPXStore *imapx_store,
                               const GString *criteria,
                               const gchar *from_function)
{
	CamelSExpResult *result;
	CamelIMAPXSearch *imapx_search = CAMEL_IMAPX_SEARCH (search);
	CamelIMAPXMailbox *mailbox;
	GPtrArray *uids = NULL;
	GError *local_error = NULL;

	mailbox = camel_imapx_folder_list_mailbox (
		CAMEL_IMAPX_FOLDER (search->folder), imapx_search->priv->cancellable, &local_error);

	/* Sanity check. */
	g_return_val_if_fail (
		((mailbox != NULL) && (local_error == NULL)) ||
		((mailbox == NULL) && (local_error != NULL)), NULL);

	if (mailbox != NULL) {
		CamelIMAPXStore *imapx_store;
		CamelIMAPXServer *imapx_server;
		const gchar *folder_name;

		imapx_store = camel_imapx_search_ref_store (imapx_search);

		/* there should always be one, held by one of the callers of this function */
		g_warn_if_fail (imapx_store != NULL);

		folder_name = camel_folder_get_full_name (search->folder);
		imapx_server = camel_imapx_store_ref_server (imapx_store, folder_name, TRUE, imapx_search->priv->cancellable, &local_error);
		if (imapx_server) {
			uids = camel_imapx_server_uid_search (imapx_server, mailbox, criteria->str, imapx_search->priv->cancellable, &local_error);
			camel_imapx_store_folder_op_done (imapx_store, imapx_server, folder_name);

			while (!uids && g_error_matches (local_error, CAMEL_IMAPX_SERVER_ERROR, CAMEL_IMAPX_SERVER_ERROR_TRY_RECONNECT)) {
				g_clear_error (&local_error);
				g_clear_object (&imapx_server);

				imapx_server = camel_imapx_store_ref_server (imapx_store, folder_name, TRUE, imapx_search->priv->cancellable, &local_error);
				if (imapx_server) {
					uids = camel_imapx_server_uid_search (imapx_server, mailbox, criteria->str, imapx_search->priv->cancellable, &local_error);
					camel_imapx_store_folder_op_done (imapx_store, imapx_server, folder_name);
				}
			}
		}

		g_clear_object (&imapx_server);
		g_clear_object (&imapx_store);
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

	if (search->current != NULL) {
		result = camel_sexp_result_new (sexp, CAMEL_SEXP_RES_BOOL);
		result->value.boolean = (uids && uids->len > 0);
	} else {
		result = camel_sexp_result_new (sexp, CAMEL_SEXP_RES_ARRAY_PTR);
		result->value.ptrarray = g_ptr_array_ref (uids);
	}

	g_ptr_array_unref (uids);

	return result;
}

static CamelSExpResult *
imapx_search_match_all (CamelSExp *sexp,
                        gint argc,
                        CamelSExpTerm **argv,
                        CamelFolderSearch *search)
{
	CamelIMAPXSearch *imapx_search = CAMEL_IMAPX_SEARCH (search);
	CamelIMAPXStore *imapx_store;
	CamelSExpResult *result;
	GPtrArray *summary;
	gint local_data_search = 0, *prev_local_data_search, ii;

	if (argc != 1)
		return imapx_search_result_match_none (sexp, search);

	imapx_store = camel_imapx_search_ref_store (CAMEL_IMAPX_SEARCH (search));
	if (!imapx_store || search->current || !search->summary) {
		g_clear_object (&imapx_store);

		/* Chain up to parent's method. */
		return CAMEL_FOLDER_SEARCH_CLASS (camel_imapx_search_parent_class)->
			match_all (sexp, argc, argv, search);
	}

	/* First try to see whether all used headers are available locally - if
	 * they are, then do not use server-side filtering at all. */
	prev_local_data_search = imapx_search->priv->local_data_search;
	imapx_search->priv->local_data_search = &local_data_search;

	summary = search->summary_set ? search->summary_set : search->summary;

	if (!CAMEL_IS_VEE_FOLDER (search->folder)) {
		camel_folder_summary_prepare_fetch_all (search->folder->summary, NULL);
	}

	for (ii = 0; ii < summary->len; ii++) {
		search->current = camel_folder_summary_get (search->folder->summary, summary->pdata[ii]);
		if (search->current) {
			result = camel_sexp_term_eval (sexp, argv[0]);
			camel_sexp_result_free (sexp, result);
			camel_message_info_unref (search->current);
			search->current = NULL;
			break;
		}
	}
	imapx_search->priv->local_data_search = prev_local_data_search;

	if (local_data_search >= 0) {
		g_clear_object (&imapx_store);

		/* Chain up to parent's method. */
		return CAMEL_FOLDER_SEARCH_CLASS (camel_imapx_search_parent_class)->
			match_all (sexp, argc, argv, search);
	}

	/* let's change the requirements a bit, the parent class expects as a result boolean,
	 * but here is expected GPtrArray of matched UIDs */
	result = camel_sexp_term_eval (sexp, argv[0]);

	g_object_unref (imapx_store);

	g_return_val_if_fail (result != NULL, result);
	g_return_val_if_fail (result->type == CAMEL_SEXP_RES_ARRAY_PTR, result);

	return result;
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
	GString *criteria;
	gint ii, jj;

	/* Always do body-search server-side */
	if (imapx_search->priv->local_data_search) {
		*imapx_search->priv->local_data_search = -1;
		return imapx_search_result_match_none (sexp, search);
	}

	/* Match everything if argv = [""] */
	if (argc == 1 && argv[0]->value.string[0] == '\0')
		return imapx_search_result_match_all (sexp, search);

	/* Match nothing if empty argv or empty summary. */
	if (argc == 0 || search->summary->len == 0)
		return imapx_search_result_match_none (sexp, search);

	imapx_store = camel_imapx_search_ref_store (CAMEL_IMAPX_SEARCH (search));

	/* This will be NULL if we're offline. Search from cache. */
	if (imapx_store == NULL) {
		/* Chain up to parent's method. */
		return CAMEL_FOLDER_SEARCH_CLASS (camel_imapx_search_parent_class)->
			body_contains (sexp, argc, argv, search);
	}

	/* Build the IMAP search criteria. */

	criteria = g_string_sized_new (128);

	if (search->current != NULL) {
		const gchar *uid;

		/* Limit the search to a single UID. */
		uid = camel_message_info_uid (search->current);
		g_string_append_printf (criteria, "UID %s", uid);
	}

	for (ii = 0; ii < argc; ii++) {
		struct _camel_search_words *words;
		const guchar *term;

		if (argv[ii]->type != CAMEL_SEXP_RES_STRING)
			continue;

		/* Handle multiple search words within a single term. */
		term = (const guchar *) argv[ii]->value.string;
		words = camel_search_words_split (term);

		for (jj = 0; jj < words->len; jj++) {
			gchar *cp;

			if (criteria->len > 0)
				g_string_append_c (criteria, ' ');

			g_string_append (criteria, "BODY \"");

			cp = words->words[jj]->word;
			for (; *cp != '\0'; cp++) {
				if (*cp == '\\' || *cp == '"')
					g_string_append_c (criteria, '\\');
				g_string_append_c (criteria, *cp);
			}

			g_string_append_c (criteria, '"');
		}
	}

	result = imapx_search_process_criteria (sexp, search, imapx_store, criteria, G_STRFUNC);

	g_string_free (criteria, TRUE);
	g_object_unref (imapx_store);

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
	GString *criteria;
	gint ii, jj;

	/* Match nothing if empty argv or empty summary. */
	if (argc <= 1 ||
	    argv[0]->type != CAMEL_SEXP_RES_STRING ||
	    search->summary->len == 0)
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

	criteria = g_string_sized_new (128);

	if (search->current != NULL) {
		const gchar *uid;

		/* Limit the search to a single UID. */
		uid = camel_message_info_uid (search->current);
		g_string_append_printf (criteria, "UID %s", uid);
	}

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

	for (ii = 1; ii < argc; ii++) {
		struct _camel_search_words *words;
		const guchar *term;

		if (argv[ii]->type != CAMEL_SEXP_RES_STRING)
			continue;

		/* Handle multiple search words within a single term. */
		term = (const guchar *) argv[ii]->value.string;
		words = camel_search_words_split (term);

		for (jj = 0; jj < words->len; jj++) {
			gchar *cp;

			if (criteria->len > 0)
				g_string_append_c (criteria, ' ');

			if (command)
				g_string_append (criteria, command);
			else
				g_string_append_printf (criteria, "HEADER \"%s\"", headername);

			g_string_append (criteria, " \"");

			cp = words->words[jj]->word;
			for (; *cp != '\0'; cp++) {
				if (*cp == '\\' || *cp == '"')
					g_string_append_c (criteria, '\\');
				g_string_append_c (criteria, *cp);
			}

			g_string_append_c (criteria, '"');
		}
	}

	result = imapx_search_process_criteria (sexp, search, imapx_store, criteria, G_STRFUNC);

	g_string_free (criteria, TRUE);
	g_object_unref (imapx_store);

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
	if (argc == 0 || search->summary->len == 0)
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

	if (search->current != NULL) {
		const gchar *uid;

		/* Limit the search to a single UID. */
		uid = camel_message_info_uid (search->current);
		g_string_append_printf (criteria, "UID %s", uid);
	}

	for (ii = 0; ii < argc; ii++) {
		const gchar *headername;

		if (argv[ii]->type != CAMEL_SEXP_RES_STRING)
			continue;

		headername = argv[ii]->value.string;

		if (criteria->len > 0)
			g_string_append_c (criteria, ' ');

		g_string_append_printf (criteria, "HEADER \"%s\" \"\"", headername);
	}

	result = imapx_search_process_criteria (sexp, search, imapx_store, criteria, G_STRFUNC);

	g_string_free (criteria, TRUE);
	g_object_unref (imapx_store);

	return result;
}

static void
camel_imapx_search_class_init (CamelIMAPXSearchClass *class)
{
	GObjectClass *object_class;
	CamelFolderSearchClass *search_class;

	g_type_class_add_private (class, sizeof (CamelIMAPXSearchPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = imapx_search_set_property;
	object_class->get_property = imapx_search_get_property;
	object_class->dispose = imapx_search_dispose;
	object_class->finalize = imapx_search_finalize;

	search_class = CAMEL_FOLDER_SEARCH_CLASS (class);
	search_class->match_all = imapx_search_match_all;
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
			G_PARAM_STATIC_STRINGS));
}

static void
camel_imapx_search_init (CamelIMAPXSearch *search)
{
	search->priv = CAMEL_IMAPX_SEARCH_GET_PRIVATE (search);
	search->priv->local_data_search = NULL;

	g_weak_ref_init (&search->priv->imapx_store, NULL);
}

/**
 * camel_imapx_search_new:
 * imapx_store: a #CamelIMAPXStore to which the search belongs
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
 * or %NULL when the store is offline.
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

	if (imapx_store && !camel_offline_store_get_online (CAMEL_OFFLINE_STORE (imapx_store)))
		g_clear_object (&imapx_store);

	return imapx_store;
}

/**
 * camel_imapx_search_set_store:
 * @search: a #CamelIMAPXSearch
 * @imapx_server: a #CamelIMAPXStore, or %NULL
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
 * Since: 3.14
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
