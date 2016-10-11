/*
 * e-gdata-oauth2-authorizer.c
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

#include "e-gdata-oauth2-authorizer.h"

#define E_GDATA_OAUTH2_AUTHORIZER_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), E_TYPE_GDATA_OAUTH2_AUTHORIZER, EGDataOAuth2AuthorizerPrivate))

struct _EGDataOAuth2AuthorizerPrivate {
	GWeakRef source;

	/* These members are protected by the global mutex. */
	gchar *access_token;
	GHashTable *authorization_domains;
	ENamedParameters *credentials;
};

enum {
	PROP_0,
	PROP_SOURCE
};

/* GDataAuthorizer methods must be thread-safe. */
static GMutex mutex;

/* Forward Declarations */
static void	e_gdata_oauth2_authorizer_interface_init
					(GDataAuthorizerInterface *iface);

G_DEFINE_TYPE_WITH_CODE (
	EGDataOAuth2Authorizer,
	e_gdata_oauth2_authorizer,
	G_TYPE_OBJECT,
	G_IMPLEMENT_INTERFACE (
		GDATA_TYPE_AUTHORIZER,
		e_gdata_oauth2_authorizer_interface_init))

static gboolean
gdata_oauth2_authorizer_is_authorized (GDataAuthorizer *authorizer,
                                       GDataAuthorizationDomain *domain)
{
	EGDataOAuth2AuthorizerPrivate *priv;

	/* This MUST be called with the mutex already locked. */

	if (domain == NULL)
		return TRUE;

	priv = E_GDATA_OAUTH2_AUTHORIZER_GET_PRIVATE (authorizer);

	return g_hash_table_contains (priv->authorization_domains, domain);
}

static void
gdata_oauth2_authorizer_set_source (EGDataOAuth2Authorizer *authorizer,
                                    ESource *source)
{
	g_return_if_fail (E_IS_SOURCE (source));

	g_weak_ref_set (&authorizer->priv->source, source);
}

static void
gdata_oauth2_authorizer_set_property (GObject *object,
                                      guint property_id,
                                      const GValue *value,
                                      GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_SOURCE:
			gdata_oauth2_authorizer_set_source (
				E_GDATA_OAUTH2_AUTHORIZER (object),
				g_value_get_object (value));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
gdata_oauth2_authorizer_get_property (GObject *object,
                                      guint property_id,
                                      GValue *value,
                                      GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_SOURCE:
			g_value_take_object (
				value,
				e_gdata_oauth2_authorizer_ref_source (
				E_GDATA_OAUTH2_AUTHORIZER (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
gdata_oauth2_authorizer_dispose (GObject *object)
{
	EGDataOAuth2AuthorizerPrivate *priv;

	priv = E_GDATA_OAUTH2_AUTHORIZER_GET_PRIVATE (object);

	g_weak_ref_set (&priv->source, NULL);

	g_hash_table_remove_all (priv->authorization_domains);

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (e_gdata_oauth2_authorizer_parent_class)->
		dispose (object);
}

static void
gdata_oauth2_authorizer_finalize (GObject *object)
{
	EGDataOAuth2AuthorizerPrivate *priv;

	priv = E_GDATA_OAUTH2_AUTHORIZER_GET_PRIVATE (object);

	g_free (priv->access_token);

	g_hash_table_destroy (priv->authorization_domains);
	g_weak_ref_clear (&priv->source);

	e_named_parameters_free (priv->credentials);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (e_gdata_oauth2_authorizer_parent_class)->
		finalize (object);
}

static void
gdata_oauth2_authorizer_constructed (GObject *object)
{
	EGDataOAuth2AuthorizerPrivate *priv;
	GType service_type;
	GList *domains;

	/* Chain up to parent's constructed() method. */
	G_OBJECT_CLASS (e_gdata_oauth2_authorizer_parent_class)->constructed (object);

	priv = E_GDATA_OAUTH2_AUTHORIZER_GET_PRIVATE (object);

	/* XXX We would need to generalize this to make the class
	 *     reusable for other service types, probably by adding
	 *     a "service-type" constructor property. */
	service_type = GDATA_TYPE_TASKS_SERVICE;
	domains = gdata_service_get_authorization_domains (service_type);

	while (domains != NULL) {
		g_hash_table_add (
			priv->authorization_domains,
			g_object_ref (domains->data));
		domains = g_list_delete_link (domains, domains);
	}
}

static void
gdata_oauth2_authorizer_process_request (GDataAuthorizer *authorizer,
                                         GDataAuthorizationDomain *domain,
                                         SoupMessage *message)
{
	EGDataOAuth2AuthorizerPrivate *priv;
	gchar *authorization;

	priv = E_GDATA_OAUTH2_AUTHORIZER_GET_PRIVATE (authorizer);

	g_mutex_lock (&mutex);

	if (!gdata_oauth2_authorizer_is_authorized (authorizer, domain))
		goto exit;

	/* We can't add an Authorization header without an access token.
	 * Let the request fail.  GData should refresh us if it gets back
	 * a "401 Authorization required" response from Google, and then
	 * automatically retry the request. */
	if (priv->access_token == NULL)
		goto exit;

	authorization = g_strdup_printf ("OAuth %s", priv->access_token);

	/* Use replace here, not append, to make sure
	 * there's only one "Authorization" header. */
	soup_message_headers_replace (
		message->request_headers,
		"Authorization", authorization);

	g_free (authorization);

exit:
	g_mutex_unlock (&mutex);
}

static gboolean
gdata_oauth2_authorizer_is_authorized_for_domain (GDataAuthorizer *authorizer,
                                                  GDataAuthorizationDomain *domain)
{
	gboolean authorized;

	g_mutex_lock (&mutex);

	authorized = gdata_oauth2_authorizer_is_authorized (authorizer, domain);

	g_mutex_unlock (&mutex);

	return authorized;
}

static gboolean
gdata_oauth2_authorizer_refresh_authorization (GDataAuthorizer *authorizer,
                                               GCancellable *cancellable,
                                               GError **error)
{
	EGDataOAuth2Authorizer *oauth2_authorizer;
	ESource *source;
	gchar **ptr_access_token;
	gboolean success = FALSE;

	oauth2_authorizer = E_GDATA_OAUTH2_AUTHORIZER (authorizer);
	source = e_gdata_oauth2_authorizer_ref_source (oauth2_authorizer);
	g_return_val_if_fail (source != NULL, FALSE);

	ptr_access_token = &oauth2_authorizer->priv->access_token;

	g_mutex_lock (&mutex);

	g_free (*ptr_access_token);
	*ptr_access_token = NULL;

	success = e_util_get_source_oauth2_access_token_sync (source, oauth2_authorizer->priv->credentials,
		ptr_access_token, NULL, cancellable, error);

	g_mutex_unlock (&mutex);

	g_object_unref (source);

	return success;
}

static void
e_gdata_oauth2_authorizer_class_init (EGDataOAuth2AuthorizerClass *class)
{
	GObjectClass *object_class;

	g_type_class_add_private (
		class, sizeof (EGDataOAuth2AuthorizerPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = gdata_oauth2_authorizer_set_property;
	object_class->get_property = gdata_oauth2_authorizer_get_property;
	object_class->dispose = gdata_oauth2_authorizer_dispose;
	object_class->finalize = gdata_oauth2_authorizer_finalize;
	object_class->constructed = gdata_oauth2_authorizer_constructed;

	g_object_class_install_property (
		object_class,
		PROP_SOURCE,
		g_param_spec_object (
			"source",
			"Source",
			"The data source to authenticate",
			E_TYPE_SOURCE,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT_ONLY |
			G_PARAM_STATIC_STRINGS));
}

static void
e_gdata_oauth2_authorizer_interface_init (GDataAuthorizerInterface *iface)
{
	iface->process_request =
		gdata_oauth2_authorizer_process_request;
	iface->is_authorized_for_domain =
		gdata_oauth2_authorizer_is_authorized_for_domain;
	iface->refresh_authorization =
		gdata_oauth2_authorizer_refresh_authorization;
}

static void
e_gdata_oauth2_authorizer_init (EGDataOAuth2Authorizer *authorizer)
{
	GHashTable *authorization_domains;

	authorization_domains = g_hash_table_new_full (
		(GHashFunc) g_direct_hash,
		(GEqualFunc) g_direct_equal,
		(GDestroyNotify) g_object_unref,
		(GDestroyNotify) NULL);

	authorizer->priv = E_GDATA_OAUTH2_AUTHORIZER_GET_PRIVATE (authorizer);
	authorizer->priv->authorization_domains = authorization_domains;
	g_weak_ref_init (&authorizer->priv->source, NULL);
}

EGDataOAuth2Authorizer *
e_gdata_oauth2_authorizer_new (ESource *source)
{
	g_return_val_if_fail (E_IS_SOURCE (source), NULL);

	return g_object_new (
		E_TYPE_GDATA_OAUTH2_AUTHORIZER,
		"source", source, NULL);
}

ESource *
e_gdata_oauth2_authorizer_ref_source (EGDataOAuth2Authorizer *authorizer)
{
	g_return_val_if_fail (E_IS_GDATA_OAUTH2_AUTHORIZER (authorizer), NULL);

	return g_weak_ref_get (&authorizer->priv->source);
}

void
e_gdata_oauth2_authorizer_set_credentials (EGDataOAuth2Authorizer *authorizer,
					   const ENamedParameters *credentials)
{
	g_return_if_fail (E_IS_GDATA_OAUTH2_AUTHORIZER (authorizer));

	g_mutex_lock (&mutex);

	e_named_parameters_free (authorizer->priv->credentials);
	if (credentials)
		authorizer->priv->credentials = e_named_parameters_new_clone (credentials);
	else
		authorizer->priv->credentials = NULL;

	g_mutex_unlock (&mutex);
}

ENamedParameters *
e_gdata_oauth2_authorizer_clone_credentials (EGDataOAuth2Authorizer *authorizer)
{
	ENamedParameters *credentials = NULL;

	g_return_val_if_fail (E_IS_GDATA_OAUTH2_AUTHORIZER (authorizer), NULL);

	g_mutex_lock (&mutex);

	if (authorizer->priv->credentials)
		credentials = e_named_parameters_new_clone (authorizer->priv->credentials);

	g_mutex_unlock (&mutex);

	return credentials;
}
