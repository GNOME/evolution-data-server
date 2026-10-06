/*
 * SPDX-FileCopyrightText: (C) 2026 Tobias Mueller <muelli@cryptobitch.de>
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

/**
 * SECTION: e-oauth2-service-dynamic
 * @include: libedataserver/libedataserver.h
 * @short_description: OAuth2 service for dynamically registered clients
 *
 * #EOAuth2ServiceDynamic is an OAuth2 service whose client data is not
 * compiled in but read from the source's #ESourceOAuth2Client extension,
 * as filled at setup time by a provider, typically through RFC 8414
 * discovery and RFC 7591 dynamic client registration. It claims sources
 * whose authentication method is "OAuth2Dynamic" and uses PKCE (RFC 7636).
 *
 * Since: 3.64
 **/

#include "evolution-data-server-config.h"

#include <string.h>
#include <glib/gi18n-lib.h>

#include "e-data-server-util.h"
#include "e-oauth2-service.h"
#include "e-oauth2-service-base.h"
#include "e-source-oauth2-client.h"

#include "e-oauth2-service-dynamic.h"

struct _EOAuth2ServiceDynamic {
	EOAuth2ServiceBase parent;
};

/* Forward Declarations */
static void e_oauth2_service_dynamic_oauth2_service_init (EOAuth2ServiceInterface *iface);

G_DEFINE_TYPE_WITH_CODE (EOAuth2ServiceDynamic, e_oauth2_service_dynamic, E_TYPE_OAUTH2_SERVICE_BASE,
	G_IMPLEMENT_INTERFACE (E_TYPE_OAUTH2_SERVICE, e_oauth2_service_dynamic_oauth2_service_init))

G_LOCK_DEFINE_STATIC (pkce_lock);
static GHashTable *pkce_verifiers = NULL; /* gchar *source_uid ~> PkceEntry * */

/* How long a pending verifier is reused instead of replaced */
#define PKCE_VERIFIER_MAX_AGE_USEC (15 * 60 * (gint64) G_USEC_PER_SEC)

typedef struct _PkceEntry {
	gchar *verifier;
	gint64 stashed_at; /* g_get_monotonic_time () */
} PkceEntry;

static void
pkce_entry_free (gpointer data)
{
	PkceEntry *entry = data;

	if (entry) {
		g_free (entry->verifier);
		g_free (entry);
	}
}

static ESourceOAuth2Client *
eos_dynamic_get_client (ESource *source)
{
	if (!source || !e_source_has_extension (source, E_SOURCE_EXTENSION_OAUTH2_CLIENT))
		return NULL;

	return e_source_get_extension (source, E_SOURCE_EXTENSION_OAUTH2_CLIENT);
}

/* Returns the current value as a borrowed string valid as long as @source,
   by interning each distinct value, so earlier answers never dangle */
static const gchar *
eos_dynamic_intern_value (ESource *source,
			  const gchar *key_name,
			  gchar * (* dup_value) (ESourceOAuth2Client *extension))
{
	G_LOCK_DEFINE_STATIC (interned_values);
	ESourceOAuth2Client *extension;
	GHashTable *seen;
	gchar *fresh;
	gpointer orig_key = NULL;

	extension = eos_dynamic_get_client (source);
	if (!extension)
		return NULL;

	fresh = dup_value (extension);
	if (!fresh || !*fresh) {
		g_free (fresh);
		return NULL;
	}

	G_LOCK (interned_values);

	seen = g_object_get_data (G_OBJECT (source), key_name);
	if (!seen) {
		seen = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
		g_object_set_data_full (G_OBJECT (source), key_name, seen, (GDestroyNotify) g_hash_table_destroy);
	}

	if (g_hash_table_lookup_extended (seen, fresh, &orig_key, NULL)) {
		g_free (fresh);
	} else {
		g_hash_table_insert (seen, fresh, fresh);
		orig_key = fresh;
	}

	G_UNLOCK (interned_values);

	return orig_key;
}

static const gchar *
eos_dynamic_get_name (EOAuth2Service *service)
{
	return "OAuth2Dynamic";
}

static const gchar *
eos_dynamic_get_display_name (EOAuth2Service *service)
{
	/* Translators: This is a user-visible string, display name of an OAuth2 service. */
	return C_("OAuth2Service", "OAuth2 (automatic)");
}

static const gchar *
eos_dynamic_get_client_id (EOAuth2Service *service,
			   ESource *source)
{
	return eos_dynamic_intern_value (source, "eds-oauth2-dynamic-client-id",
		e_source_oauth2_client_dup_client_id);
}

static const gchar *
eos_dynamic_get_client_secret (EOAuth2Service *service,
			       ESource *source)
{
	return eos_dynamic_intern_value (source, "eds-oauth2-dynamic-client-secret",
		e_source_oauth2_client_dup_client_secret);
}

static const gchar *
eos_dynamic_get_authentication_uri (EOAuth2Service *service,
				    ESource *source)
{
	return eos_dynamic_intern_value (source, "eds-oauth2-dynamic-authorization-endpoint",
		e_source_oauth2_client_dup_authorization_endpoint);
}

static const gchar *
eos_dynamic_get_refresh_uri (EOAuth2Service *service,
			     ESource *source)
{
	return eos_dynamic_intern_value (source, "eds-oauth2-dynamic-token-endpoint",
		e_source_oauth2_client_dup_token_endpoint);
}

static const gchar *
eos_dynamic_get_redirect_uri (EOAuth2Service *service,
			      ESource *source)
{
	const gchar *value;

	value = eos_dynamic_intern_value (source, "eds-oauth2-dynamic-redirect-uri",
		e_source_oauth2_client_dup_redirect_uri);

	return value ? value : "urn:ietf:wg:oauth:2.0:oob";
}

static gchar *
eos_dynamic_base64url (const guchar *data,
		       gsize length)
{
	gchar *text = g_base64_encode (data, length);
	gchar *pos;

	for (pos = text; *pos; pos++) {
		if (*pos == '+')
			*pos = '-';
		else if (*pos == '/')
			*pos = '_';
		else if (*pos == '=') {
			*pos = '\0';
			break;
		}
	}

	return text;
}

/* Returns the S256 challenge, reusing the pending verifier, because several
   authentication URIs (like "Copy URL") can be in play for one source */
static gchar *
eos_dynamic_stash_pkce_challenge (ESource *source)
{
	const gchar *uid;
	gchar *verifier;
	gchar *challenge;
	GChecksum *checksum;
	guint8 digest[32];
	gsize digest_len = sizeof (digest);
	PkceEntry *entry;

	uid = e_source_get_uid (source);

	G_LOCK (pkce_lock);

	if (!pkce_verifiers)
		pkce_verifiers = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, pkce_entry_free);

	entry = g_hash_table_lookup (pkce_verifiers, uid);
	if (entry && g_get_monotonic_time () - entry->stashed_at <= PKCE_VERIFIER_MAX_AGE_USEC) {
		verifier = g_strdup (entry->verifier);
	} else {
		guchar random_bytes[32];

		e_util_fill_random_bytes (random_bytes, sizeof (random_bytes));

		verifier = eos_dynamic_base64url (random_bytes, sizeof (random_bytes));

		entry = g_new (PkceEntry, 1);
		entry->verifier = g_strdup (verifier);
		entry->stashed_at = g_get_monotonic_time ();

		g_hash_table_insert (pkce_verifiers, g_strdup (uid), entry);
	}

	G_UNLOCK (pkce_lock);

	checksum = g_checksum_new (G_CHECKSUM_SHA256);
	g_checksum_update (checksum, (const guchar *) verifier, strlen (verifier));
	g_checksum_get_digest (checksum, digest, &digest_len);
	g_checksum_free (checksum);
	g_free (verifier);

	challenge = eos_dynamic_base64url (digest, digest_len);

	return challenge;
}

/* Removes and returns the verifier stashed for @source, or %NULL if none */
static gchar *
eos_dynamic_redeem_pkce_verifier (ESource *source)
{
	const gchar *uid;
	gchar *verifier = NULL;

	uid = e_source_get_uid (source);

	G_LOCK (pkce_lock);

	if (pkce_verifiers) {
		PkceEntry *entry = g_hash_table_lookup (pkce_verifiers, uid);

		if (entry)
			verifier = g_strdup (entry->verifier);

		g_hash_table_remove (pkce_verifiers, uid);
	}

	G_UNLOCK (pkce_lock);

	return verifier;
}

static void
eos_dynamic_prepare_authentication_uri_query (EOAuth2Service *service,
					      ESource *source,
					      GHashTable *uri_query)
{
	ESourceOAuth2Client *extension;
	gchar *value;

	g_return_if_fail (uri_query != NULL);

	extension = eos_dynamic_get_client (source);
	if (!extension)
		return;

	value = e_source_oauth2_client_dup_scope (extension);
	if (value)
		e_oauth2_service_util_take_to_form (uri_query, "scope", value);

	value = e_source_oauth2_client_dup_resource (extension);
	if (value)
		e_oauth2_service_util_take_to_form (uri_query, "resource", value);

	/* PKCE, RFC 7636; required by many providers for public clients */
	value = eos_dynamic_stash_pkce_challenge (source);
	e_oauth2_service_util_take_to_form (uri_query, "code_challenge", value);
	e_oauth2_service_util_set_to_form (uri_query, "code_challenge_method", "S256");
}

static void
eos_dynamic_prepare_get_token_form (EOAuth2Service *service,
				    ESource *source,
				    const gchar *authorization_code,
				    GHashTable *form)
{
	ESourceOAuth2Client *extension;
	gchar *value, *verifier = NULL;

	g_return_if_fail (form != NULL);

	extension = eos_dynamic_get_client (source);
	if (!extension)
		return;

	value = e_source_oauth2_client_dup_resource (extension);
	if (value)
		e_oauth2_service_util_take_to_form (form, "resource", value);

	verifier = eos_dynamic_redeem_pkce_verifier (source);
	if (verifier)
		e_oauth2_service_util_take_to_form (form, "code_verifier", verifier);
}

static void
eos_dynamic_prepare_refresh_token_form (EOAuth2Service *service,
					ESource *source,
					const gchar *refresh_token,
					GHashTable *form)
{
	ESourceOAuth2Client *extension;
	gchar *value;

	g_return_if_fail (form != NULL);

	extension = eos_dynamic_get_client (source);
	if (!extension)
		return;

	value = e_source_oauth2_client_dup_resource (extension);
	if (value)
		e_oauth2_service_util_take_to_form (form, "resource", value);
}

static void
e_oauth2_service_dynamic_oauth2_service_init (EOAuth2ServiceInterface *iface)
{
	iface->get_name = eos_dynamic_get_name;
	iface->get_display_name = eos_dynamic_get_display_name;
	iface->get_client_id = eos_dynamic_get_client_id;
	iface->get_client_secret = eos_dynamic_get_client_secret;
	iface->get_authentication_uri = eos_dynamic_get_authentication_uri;
	iface->get_refresh_uri = eos_dynamic_get_refresh_uri;
	iface->get_redirect_uri = eos_dynamic_get_redirect_uri;
	iface->prepare_authentication_uri_query = eos_dynamic_prepare_authentication_uri_query;
	iface->prepare_get_token_form = eos_dynamic_prepare_get_token_form;
	iface->prepare_refresh_token_form = eos_dynamic_prepare_refresh_token_form;
}

static void
e_oauth2_service_dynamic_class_init (EOAuth2ServiceDynamicClass *klass)
{
}

static void
e_oauth2_service_dynamic_init (EOAuth2ServiceDynamic *oauth2_dynamic)
{
}
