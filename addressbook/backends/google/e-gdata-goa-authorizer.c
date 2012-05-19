/*
 * e-gdata-goa-authorizer.c
 *
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
 */

#include "e-gdata-goa-authorizer.h"

#include <string.h>
#include <oauth.h>

#define E_GDATA_GOA_AUTHORIZER_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), E_TYPE_GDATA_GOA_AUTHORIZER, EGDataGoaAuthorizerPrivate))

struct _EGDataGoaAuthorizerPrivate {

	/* GoaObject is already thread-safe. */
	GoaObject *goa_object;

	/* These members are protected by the global mutex. */
	gchar *access_token;
	gchar *access_token_secret;
	GHashTable *authorization_domains;
};

enum {
	PROP_0,
	PROP_GOA_OBJECT
};

/* GDataAuthorizer methods must be thread-safe. */
static GStaticMutex mutex = G_STATIC_MUTEX_INIT;

/* Forward Declarations */
static void	e_gdata_goa_authorizer_interface_init
					(GDataAuthorizerInterface *interface);

G_DEFINE_TYPE_WITH_CODE (
	EGDataGoaAuthorizer,
	e_gdata_goa_authorizer,
	G_TYPE_OBJECT,
	G_IMPLEMENT_INTERFACE (
		GDATA_TYPE_AUTHORIZER,
		e_gdata_goa_authorizer_interface_init))

static GHashTable *
gdata_goa_authorizer_get_parameters (SoupMessage *message,
                                     const gchar *consumer_key,
                                     const gchar *consumer_secret,
                                     const gchar *access_token,
                                     const gchar *access_token_secret)
{
	GString *query;
	GString *base_string;
	GString *signing_key;
	GHashTable *parameters;
	GHashTableIter iter;
	SoupURI *soup_uri;
	GList *keys, *i;
	gchar *string;
	gchar *request_uri;
	gpointer key;

	parameters = g_hash_table_new_full (
		(GHashFunc) g_str_hash,
		(GEqualFunc) g_str_equal,
		(GDestroyNotify) NULL,
		(GDestroyNotify) g_free);

	/* soup_form_decode() uses an awkward allocation style for
	 * its hash table entries, so it's easier to copy its content
	 * into our own hash table than try to use the returned hash
	 * table directly. */

	soup_uri = soup_message_get_uri (message);
	if (soup_uri->query != NULL) {
		GHashTable *hash_table;
		gpointer value;

		hash_table = soup_form_decode (soup_uri->query);
		g_hash_table_iter_init (&iter, hash_table);
		while (g_hash_table_iter_next (&iter, &key, &value)) {
			key = (gpointer) g_intern_string (key);
			value = g_strdup (value);
			g_hash_table_insert (parameters, key, value);
		}
		g_hash_table_destroy (hash_table);
	}

	/* Add OAuth parameters. */

	key = (gpointer) "oauth_version";
	g_hash_table_insert (parameters, key, g_strdup ("1.0"));

	string = oauth_gen_nonce ();
	key = (gpointer) "oauth_nonce";
	g_hash_table_insert (parameters, key, g_strdup (string));
	free (string);  /* do not use g_free () */

	key = (gpointer) "oauth_timestamp";
	string = g_strdup_printf ("%" G_GINT64_FORMAT, (gint64) time (NULL));
	g_hash_table_insert (parameters, key, string); /* takes ownership */

	key = (gpointer) "oauth_consumer_key";
	g_hash_table_insert (parameters, key, g_strdup (consumer_key));

	key = (gpointer) "oauth_token";
	g_hash_table_insert (parameters, key, g_strdup (access_token));

	key = (gpointer) "oauth_signature_method";
	g_hash_table_insert (parameters, key, g_strdup ("HMAC-SHA1"));

	/* Build the query part of the signature base string. */

	query = g_string_sized_new (512);
	keys = g_hash_table_get_keys (parameters);
	keys = g_list_sort (keys, (GCompareFunc) g_strcmp0);
	for (i = keys; i != NULL; i = g_list_next (i)) {
		const gchar *_key = i->data;
		const gchar *val;

		val = g_hash_table_lookup (parameters, _key);

		if (i != keys)
			g_string_append_c (query, '&');

		g_string_append_uri_escaped (query, _key, NULL, FALSE);
		g_string_append_c (query, '=');
		g_string_append_uri_escaped (query, val, NULL, FALSE);
	}
	g_list_free (keys);

	/* Build the signature base string. */

	soup_uri = soup_uri_copy (soup_uri);
	soup_uri_set_query (soup_uri, NULL);
	soup_uri_set_fragment (soup_uri, NULL);
	request_uri = soup_uri_to_string (soup_uri, FALSE);
	soup_uri_free (soup_uri);

	base_string = g_string_sized_new (512);
	g_string_append_uri_escaped (base_string, message->method, NULL, FALSE);
	g_string_append_c (base_string, '&');
	g_string_append_uri_escaped (base_string, request_uri, NULL, FALSE);
	g_string_append_c (base_string, '&');
	g_string_append_uri_escaped (base_string, query->str, NULL, FALSE);

	/* Build the HMAC-SHA1 signing key. */

	signing_key = g_string_sized_new (512);
	g_string_append_uri_escaped (
		signing_key, consumer_secret, NULL, FALSE);
	g_string_append_c (signing_key, '&');
	g_string_append_uri_escaped (
		signing_key, access_token_secret, NULL, FALSE);

	/* Sign the request. */

	key = (gpointer) "oauth_signature";
	string = oauth_sign_hmac_sha1 (base_string->str, signing_key->str);
	g_hash_table_insert (parameters, key, g_strdup (string));
	free (string);  /* do not use g_free () */

	g_free (request_uri);

	g_string_free (query, TRUE);
	g_string_free (base_string, TRUE);
	g_string_free (signing_key, TRUE);

	return parameters;
}

static void
gdata_goa_authorizer_add_authorization (GDataAuthorizer *authorizer,
                                        SoupMessage *message)
{
	EGDataGoaAuthorizerPrivate *priv;
	GoaOAuthBased *goa_oauth_based;
	GHashTable *parameters;
	GString *authorization;
	const gchar *consumer_key;
	const gchar *consumer_secret;
	guint ii;

	const gchar *oauth_keys[] = {
		"oauth_version",
		"oauth_nonce",
		"oauth_timestamp",
		"oauth_consumer_key",
		"oauth_token",
		"oauth_signature_method",
		"oauth_signature"
	};

	/* This MUST be called with the mutex already locked. */

	priv = E_GDATA_GOA_AUTHORIZER_GET_PRIVATE (authorizer);

	/* We can't add an Authorization header without an access token.
	 * Let the request fail.  GData should refresh us if it gets back
	 * a "401 Authorization required" response from Google, and then
	 * automatically retry the request. */
	if (priv->access_token == NULL)
		return;

	goa_oauth_based = goa_object_get_oauth_based (priv->goa_object);

	consumer_key = goa_oauth_based_get_consumer_key (goa_oauth_based);
	consumer_secret = goa_oauth_based_get_consumer_secret (goa_oauth_based);

	parameters = gdata_goa_authorizer_get_parameters (
		message,
		consumer_key,
		consumer_secret,
		priv->access_token,
		priv->access_token_secret);

	authorization = g_string_new ("OAuth ");

	for (ii = 0; ii < G_N_ELEMENTS (oauth_keys); ii++) {
		const gchar *key;
		const gchar *val;

		key = oauth_keys[ii];
		val = g_hash_table_lookup (parameters, key);

		if (ii > 0)
			g_string_append (authorization, ", ");

		g_string_append (authorization, key);
		g_string_append_c (authorization, '=');
		g_string_append_c (authorization, '"');
		g_string_append_uri_escaped (authorization, val, NULL, FALSE);
		g_string_append_c (authorization, '"');
	}

	/* Use replace here, not append, to make sure
	 * there's only one "Authorization" header. */
	soup_message_headers_replace (
		message->request_headers,
		"Authorization", authorization->str);

	g_string_free (authorization, TRUE);
	g_hash_table_destroy (parameters);

	g_object_unref (goa_oauth_based);
}

static gboolean
gdata_goa_authorizer_is_authorized (GDataAuthorizer *authorizer,
                                    GDataAuthorizationDomain *domain)
{
	EGDataGoaAuthorizerPrivate *priv;

	/* This MUST be called with the mutex already locked. */

	if (domain == NULL)
		return TRUE;

	priv = E_GDATA_GOA_AUTHORIZER_GET_PRIVATE (authorizer);
	domain = g_hash_table_lookup (priv->authorization_domains, domain);

	return (domain != NULL);
}

static void
gdata_goa_authorizer_set_goa_object (EGDataGoaAuthorizer *authorizer,
                                     GoaObject *goa_object)
{
	g_return_if_fail (GOA_IS_OBJECT (goa_object));
	g_return_if_fail (authorizer->priv->goa_object == NULL);

	authorizer->priv->goa_object = g_object_ref (goa_object);
}

static void
gdata_goa_authorizer_set_property (GObject *object,
                                   guint property_id,
                                   const GValue *value,
                                   GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_GOA_OBJECT:
			gdata_goa_authorizer_set_goa_object (
				E_GDATA_GOA_AUTHORIZER (object),
				g_value_get_object (value));
			return;
		default:
			g_assert_not_reached ();
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
gdata_goa_authorizer_get_property (GObject *object,
                                   guint property_id,
                                   GValue *value,
                                   GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_GOA_OBJECT:
			g_value_set_object (
				value,
				e_gdata_goa_authorizer_get_goa_object (
				E_GDATA_GOA_AUTHORIZER (object)));
			return;
		default:
			g_assert_not_reached ();
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
gdata_goa_authorizer_dispose (GObject *object)
{
	EGDataGoaAuthorizerPrivate *priv;

	priv = E_GDATA_GOA_AUTHORIZER_GET_PRIVATE (object);

	if (priv->goa_object != NULL) {
		g_object_unref (priv->goa_object);
		priv->goa_object = NULL;
	}

	g_hash_table_remove_all (priv->authorization_domains);

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (e_gdata_goa_authorizer_parent_class)->dispose (object);
}

static void
gdata_goa_authorizer_finalize (GObject *object)
{
	EGDataGoaAuthorizerPrivate *priv;

	priv = E_GDATA_GOA_AUTHORIZER_GET_PRIVATE (object);

	g_free (priv->access_token);
	g_free (priv->access_token_secret);
	g_hash_table_destroy (priv->authorization_domains);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (e_gdata_goa_authorizer_parent_class)->finalize (object);
}

static void
gdata_goa_authorizer_constructed (GObject *object)
{
	EGDataGoaAuthorizerPrivate *priv;
	GType service_type;
	GList *domains;

	/* Chain up to parent's constructed() method. */
	G_OBJECT_CLASS (e_gdata_goa_authorizer_parent_class)->
		constructed (object);

	priv = E_GDATA_GOA_AUTHORIZER_GET_PRIVATE (object);

	/* XXX We would need to generalize this to make the class
	 *     reusable for other service types, probably by adding
	 *     a "service-type" constructor property. */
	service_type = GDATA_TYPE_CONTACTS_SERVICE;
	domains = gdata_service_get_authorization_domains (service_type);

	while (domains != NULL) {
		g_hash_table_insert (
			priv->authorization_domains,
			g_object_ref (domains->data),
			domains->data);
		domains = g_list_delete_link (domains, domains);
	}
}

static void
gdata_goa_authorizer_process_request (GDataAuthorizer *authorizer,
                                      GDataAuthorizationDomain *domain,
                                      SoupMessage *message)
{
	g_static_mutex_lock (&mutex);

	if (gdata_goa_authorizer_is_authorized (authorizer, domain))
		gdata_goa_authorizer_add_authorization (authorizer, message);

	g_static_mutex_unlock (&mutex);
}

static gboolean
gdata_goa_authorizer_is_authorized_for_domain (GDataAuthorizer *authorizer,
                                               GDataAuthorizationDomain *domain)
{
	gboolean authorized;

	g_static_mutex_lock (&mutex);

	authorized = gdata_goa_authorizer_is_authorized (authorizer, domain);

	g_static_mutex_unlock (&mutex);

	return authorized;
}

static gboolean
gdata_goa_authorizer_refresh_authorization (GDataAuthorizer *authorizer,
                                            GCancellable *cancellable,
                                            GError **error)
{
	EGDataGoaAuthorizerPrivate *priv;
	GoaOAuthBased *goa_oauth_based;
	GoaAccount *goa_account;
	gboolean success = TRUE;

	priv = E_GDATA_GOA_AUTHORIZER_GET_PRIVATE (authorizer);

	g_static_mutex_lock (&mutex);

	g_free (priv->access_token);
	priv->access_token = NULL;

	g_free (priv->access_token_secret);
	priv->access_token_secret = NULL;

	goa_account = goa_object_get_account (priv->goa_object);
	goa_oauth_based = goa_object_get_oauth_based (priv->goa_object);

	success &= goa_account_call_ensure_credentials_sync (
		goa_account, NULL, cancellable, error);

	success &= goa_oauth_based_call_get_access_token_sync (
		goa_oauth_based, &priv->access_token,
		&priv->access_token_secret, NULL, cancellable, error);

	g_object_unref (goa_account);
	g_object_unref (goa_oauth_based);

	g_static_mutex_unlock (&mutex);

	return success;
}

static void
e_gdata_goa_authorizer_class_init (EGDataGoaAuthorizerClass *class)
{
	GObjectClass *object_class;

	g_type_class_add_private (class, sizeof (EGDataGoaAuthorizerPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = gdata_goa_authorizer_set_property;
	object_class->get_property = gdata_goa_authorizer_get_property;
	object_class->dispose = gdata_goa_authorizer_dispose;
	object_class->finalize = gdata_goa_authorizer_finalize;
	object_class->constructed = gdata_goa_authorizer_constructed;

	g_object_class_install_property (
		object_class,
		PROP_GOA_OBJECT,
		g_param_spec_object (
			"goa-object",
			"GoaObject",
			"The GOA account to authenticate",
			GOA_TYPE_OBJECT,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT_ONLY |
			G_PARAM_STATIC_STRINGS));
}

static void
e_gdata_goa_authorizer_interface_init (GDataAuthorizerInterface *interface)
{
	interface->process_request =
		gdata_goa_authorizer_process_request;
	interface->is_authorized_for_domain =
		gdata_goa_authorizer_is_authorized_for_domain;
	interface->refresh_authorization =
		gdata_goa_authorizer_refresh_authorization;
}

static void
e_gdata_goa_authorizer_init (EGDataGoaAuthorizer *authorizer)
{
	GHashTable *authorization_domains;

	authorization_domains = g_hash_table_new_full (
		(GHashFunc) g_direct_hash,
		(GEqualFunc) g_direct_equal,
		(GDestroyNotify) g_object_unref,
		(GDestroyNotify) NULL);

	authorizer->priv = E_GDATA_GOA_AUTHORIZER_GET_PRIVATE (authorizer);
	authorizer->priv->authorization_domains = authorization_domains;
}

EGDataGoaAuthorizer *
e_gdata_goa_authorizer_new (GoaObject *goa_object)
{
	g_return_val_if_fail (GOA_IS_OBJECT (goa_object), NULL);

	return g_object_new (
		E_TYPE_GDATA_GOA_AUTHORIZER,
		"goa-object", goa_object, NULL);
}

GoaObject *
e_gdata_goa_authorizer_get_goa_object (EGDataGoaAuthorizer *authorizer)
{
	g_return_val_if_fail (E_IS_GDATA_GOA_AUTHORIZER (authorizer), NULL);

	return authorizer->priv->goa_object;
}
