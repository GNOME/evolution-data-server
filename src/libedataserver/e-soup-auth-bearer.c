/*
 * e-soup-auth-bearer.c
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

/**
 * SECTION: e-soup-auth-bearer
 * @include: libedataserver/libedataserver.h
 * @short_description: OAuth 2.0 support for libsoup
 *
 * #ESoupAuthBearer adds libsoup support for the use of bearer tokens in
 * HTTP requests to access OAuth 2.0 protected resources, as defined in
 * <ulink url="http://tools.ietf.org/html/rfc6750">RFC 6750</ulink>.
 *
 * An #EBackend should integrate #ESoupAuthBearer first by adding it as a
 * feature to a #SoupSession's #SoupAuthManager, then from a #SoupSession
 * #SoupSession::authenticate handler call e_source_get_oauth2_access_token()
 * and pass the results to e_soup_auth_bearer_set_access_token().
 **/

#include "evolution-data-server-config.h"

#include "e-data-server-util.h"
#include "e-time-utils.h"
#include "e-soup-auth-bearer.h"

#include <time.h>

#define AUTH_STRENGTH 1

#define EXPIRY_INVALID ((time_t) -1)

/* How many seconds earlier than reported by the server is the token considered expired. */
#define TOKEN_VALIDITY_GAP_SECS 5

struct _ESoupAuthBearerPrivate {
	GMutex property_lock;
	gchar *access_token;
	time_t expiry;
};

G_DEFINE_TYPE_WITH_PRIVATE (
	ESoupAuthBearer,
	e_soup_auth_bearer,
	SOUP_TYPE_AUTH)

static gboolean
e_soup_auth_bearer_debug_enabled (void)
{
	static gint oauth2_debug = -1;

	if (oauth2_debug == -1)
		oauth2_debug = g_strcmp0 (g_getenv ("OAUTH2_DEBUG"), "1") == 0 ? 1 : 0;

	return oauth2_debug != 0;
}

static void
e_soup_auth_bearer_debug_print (const gchar *format,
				...) G_GNUC_PRINTF (1, 2);

static void
e_soup_auth_bearer_debug_print (const gchar *format,
				...)
{
	va_list args;

	if (!e_soup_auth_bearer_debug_enabled ())
		return;

	va_start (args, format);
	e_util_debug_printv ("BEARER", format, args);
	va_end (args);
}

static gboolean
e_soup_auth_bearer_is_expired_locked (ESoupAuthBearer *bearer)
{
	gboolean expired = TRUE;

	if (bearer->priv->expiry != EXPIRY_INVALID)
		expired = (bearer->priv->expiry <= time (NULL));

	return expired;
}

static void
e_soup_auth_bearer_finalize (GObject *object)
{
	ESoupAuthBearerPrivate *priv;

	priv = E_SOUP_AUTH_BEARER (object)->priv;

	g_mutex_clear (&priv->property_lock);
	g_free (priv->access_token);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (e_soup_auth_bearer_parent_class)->finalize (object);
}

static gboolean
e_soup_auth_bearer_update (SoupAuth *auth,
                           SoupMessage *message,
                           GHashTable *auth_header)
{
	ESoupAuthBearer *bearer;

	g_return_val_if_fail (E_IS_SOUP_AUTH_BEARER (auth), FALSE);

	bearer = E_SOUP_AUTH_BEARER (auth);

	if (message && soup_message_get_status (message) == SOUP_STATUS_UNAUTHORIZED) {
		g_mutex_lock (&bearer->priv->property_lock);

		/* Expire the token, it's likely to be invalid. */
		bearer->priv->expiry = EXPIRY_INVALID;

		e_soup_auth_bearer_debug_print ("%s: bearer:%p message:%p token:%p did set it as expired\n", G_STRFUNC, bearer, message, bearer->priv->access_token);

		g_mutex_unlock (&bearer->priv->property_lock);

		return FALSE;
	} else if (message) {
		g_mutex_lock (&bearer->priv->property_lock);

		e_soup_auth_bearer_debug_print ("%s: bearer:%p message:%p message status is not UNAUTHORIZED, but %u; token:%p expired:%d\n",
			G_STRFUNC, bearer, message, soup_message_get_status (message),
			bearer->priv->access_token, e_soup_auth_bearer_is_expired_locked (bearer));

		g_mutex_unlock (&bearer->priv->property_lock);
	} else {
		g_mutex_lock (&bearer->priv->property_lock);

		e_soup_auth_bearer_debug_print ("%s: bearer:%p no message; token:%p expired:%d\n",
			G_STRFUNC, bearer, bearer->priv->access_token, e_soup_auth_bearer_is_expired_locked (bearer));

		g_mutex_unlock (&bearer->priv->property_lock);
	}

	return TRUE;
}

static GSList *
e_soup_auth_bearer_get_protection_space (SoupAuth *auth,
                                         GUri *source_uri)
{
	/* XXX Not sure what to do here.  Need to return something. */

	return g_slist_prepend (NULL, g_strdup (""));
}

static gboolean
e_soup_auth_bearer_is_authenticated (SoupAuth *auth)
{
	ESoupAuthBearer *bearer;
	gboolean authenticated = FALSE;

	bearer = E_SOUP_AUTH_BEARER (auth);

	g_mutex_lock (&bearer->priv->property_lock);

	authenticated = (bearer->priv->access_token != NULL) &&
		!e_soup_auth_bearer_is_expired_locked (bearer);

	e_soup_auth_bearer_debug_print ("%s: bearer:%p token:%p expired:%d authenticated:%d\n",
		G_STRFUNC, bearer, bearer->priv->access_token,
		e_soup_auth_bearer_is_expired_locked (bearer), authenticated);

	g_mutex_unlock (&bearer->priv->property_lock);

	return authenticated;
}

static gchar *
e_soup_auth_bearer_get_authorization (SoupAuth *auth,
                                      SoupMessage *message)
{
	ESoupAuthBearer *bearer;
	gchar *res;

	bearer = E_SOUP_AUTH_BEARER (auth);

	g_mutex_lock (&bearer->priv->property_lock);

	res = g_strdup_printf ("Bearer %s", bearer->priv->access_token);

	e_soup_auth_bearer_debug_print ("%s: bearer:%p message:%p; status:%u token:%p expired:%d\n",
		G_STRFUNC, bearer, message, soup_message_get_status (message),
		bearer->priv->access_token, e_soup_auth_bearer_is_expired_locked (bearer));

	g_mutex_unlock (&bearer->priv->property_lock);

	return res;
}

static gboolean
e_soup_auth_bearer_can_authenticate (SoupAuth *auth)
{
	return FALSE;
}

static void
e_soup_auth_bearer_authenticate (SoupAuth *auth,
				 const gchar *username,
				 const gchar *password)
{
	/* Not applicable here */
	e_soup_auth_bearer_debug_print ("%s: bearer:%p tried to authenticate with username:'%s' password:%p\n",
		G_STRFUNC, auth, username, password);
}

static void
e_soup_auth_bearer_class_init (ESoupAuthBearerClass *class)
{
	GObjectClass *object_class;
	SoupAuthClass *auth_class;

	/* Keep the "e" prefix on private methods
	 * so we don't step on libsoup's namespace. */

	object_class = G_OBJECT_CLASS (class);
	object_class->finalize = e_soup_auth_bearer_finalize;

	auth_class = SOUP_AUTH_CLASS (class);
	auth_class->scheme_name = "Bearer";
	auth_class->strength = AUTH_STRENGTH;
	auth_class->update = e_soup_auth_bearer_update;
	auth_class->get_protection_space = e_soup_auth_bearer_get_protection_space;
	auth_class->is_authenticated = e_soup_auth_bearer_is_authenticated;
	auth_class->get_authorization = e_soup_auth_bearer_get_authorization;
	auth_class->can_authenticate = e_soup_auth_bearer_can_authenticate;
	auth_class->authenticate = e_soup_auth_bearer_authenticate;
}

static void
e_soup_auth_bearer_init (ESoupAuthBearer *bearer)
{
	bearer->priv = e_soup_auth_bearer_get_instance_private (bearer);
	bearer->priv->expiry = EXPIRY_INVALID;

	g_mutex_init (&bearer->priv->property_lock);
}

/**
 * e_soup_auth_bearer_set_access_token:
 * @bearer: an #ESoupAuthBearer
 * @access_token: an OAuth 2.0 access token
 * @expires_in_seconds: expiry for @access_token, or 0 if unknown
 *
 * This function is analogous to soup_auth_authenticate() for "Basic" HTTP
 * authentication, except it takes an OAuth 2.0 access token instead of a
 * username and password.
 *
 * If @expires_in_seconds is greater than zero, soup_auth_is_authenticated()
 * will return %FALSE after the given number of seconds have elapsed.
 *
 * Since: 3.10
 **/
void
e_soup_auth_bearer_set_access_token (ESoupAuthBearer *bearer,
                                     const gchar *access_token,
                                     gint expires_in_seconds)
{
	gboolean was_authenticated;
	gboolean now_authenticated;
	gboolean changed;

	g_return_if_fail (E_IS_SOUP_AUTH_BEARER (bearer));

	was_authenticated = soup_auth_is_authenticated (SOUP_AUTH (bearer));

	g_mutex_lock (&bearer->priv->property_lock);

	changed = g_strcmp0 (bearer->priv->access_token, access_token) != 0;

	if (changed) {
		g_free (bearer->priv->access_token);
		bearer->priv->access_token = g_strdup (access_token);
	}

	if (expires_in_seconds > 0)
		bearer->priv->expiry = time (NULL) + expires_in_seconds - TOKEN_VALIDITY_GAP_SECS;
	else
		bearer->priv->expiry = EXPIRY_INVALID;

	if (e_soup_auth_bearer_debug_enabled ()) {
		gchar time_str[128];

		if (bearer->priv->expiry != EXPIRY_INVALID) {
			time_t tt = (time_t) bearer->priv->expiry;
			e_time_format_time (localtime (&tt), TRUE, TRUE, time_str, sizeof (time_str));
		} else {
			time_str[0] = '-';
			time_str[1] = '1';
			time_str[2] = '\0';
		}

		e_soup_auth_bearer_debug_print ("%s: bearer:%p token:%p changed:%d expires in %d seconds, at %s\n",
			G_STRFUNC, bearer, bearer->priv->access_token, changed, expires_in_seconds, time_str);
	}

	g_mutex_unlock (&bearer->priv->property_lock);

	if (changed) {
		now_authenticated = soup_auth_is_authenticated (SOUP_AUTH (bearer));

		if (was_authenticated != now_authenticated)
			g_object_notify (G_OBJECT (bearer), "is-authenticated");
	}
}

/**
 * e_soup_auth_bearer_is_expired:
 * @bearer: an #ESoupAuthBearer
 *
 * Returns: Whether the set token is expired. It is considered expired even
 *   if the e_soup_auth_bearer_set_access_token() was called set yet.
 *
 * Since: 3.24
 **/
gboolean
e_soup_auth_bearer_is_expired (ESoupAuthBearer *bearer)
{
	gboolean expired;

	g_return_val_if_fail (E_IS_SOUP_AUTH_BEARER (bearer), TRUE);

	g_mutex_lock (&bearer->priv->property_lock);
	expired = e_soup_auth_bearer_is_expired_locked (bearer);
	g_mutex_unlock (&bearer->priv->property_lock);

	return expired;
}
