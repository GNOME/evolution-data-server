/*
 * e-source-authentication.c
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

/**
 * SECTION: e-source-authentication
 * @include: libedataserver/libedataserver.h
 * @short_description: #ESource extension for authentication settings
 *
 * The #ESourceAuthentication extension tracks authentication settings
 * for a user account on a remote server.
 *
 * Access the extension as follows:
 *
 * |[
 *   #include <libedataserver/libedataserver.h>
 *
 *   ESourceAuthentication *extension;
 *
 *   extension = e_source_get_extension (source, E_SOURCE_EXTENSION_AUTHENTICATION);
 * ]|
 **/

#include "e-source-authentication.h"

#include <libedataserver/e-data-server-util.h>

#define E_SOURCE_AUTHENTICATION_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), E_TYPE_SOURCE_AUTHENTICATION, ESourceAuthenticationPrivate))

struct _ESourceAuthenticationPrivate {
	GMutex *property_lock;
	gchar *host;
	gchar *method;
	guint16 port;
	gchar *user;
};

enum {
	PROP_0,
	PROP_HOST,
	PROP_METHOD,
	PROP_PORT,
	PROP_USER
};

G_DEFINE_TYPE (
	ESourceAuthentication,
	e_source_authentication,
	E_TYPE_SOURCE_EXTENSION)

static void
source_authentication_set_property (GObject *object,
                                    guint property_id,
                                    const GValue *value,
                                    GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_HOST:
			e_source_authentication_set_host (
				E_SOURCE_AUTHENTICATION (object),
				g_value_get_string (value));
			return;

		case PROP_METHOD:
			e_source_authentication_set_method (
				E_SOURCE_AUTHENTICATION (object),
				g_value_get_string (value));
			return;

		case PROP_PORT:
			e_source_authentication_set_port (
				E_SOURCE_AUTHENTICATION (object),
				g_value_get_uint (value));
			return;

		case PROP_USER:
			e_source_authentication_set_user (
				E_SOURCE_AUTHENTICATION (object),
				g_value_get_string (value));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
source_authentication_get_property (GObject *object,
                                    guint property_id,
                                    GValue *value,
                                    GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_HOST:
			g_value_take_string (
				value,
				e_source_authentication_dup_host (
				E_SOURCE_AUTHENTICATION (object)));
			return;

		case PROP_METHOD:
			g_value_take_string (
				value,
				e_source_authentication_dup_method (
				E_SOURCE_AUTHENTICATION (object)));
			return;

		case PROP_PORT:
			g_value_set_uint (
				value,
				e_source_authentication_get_port (
				E_SOURCE_AUTHENTICATION (object)));
			return;

		case PROP_USER:
			g_value_take_string (
				value,
				e_source_authentication_dup_user (
				E_SOURCE_AUTHENTICATION (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
source_authentication_finalize (GObject *object)
{
	ESourceAuthenticationPrivate *priv;

	priv = E_SOURCE_AUTHENTICATION_GET_PRIVATE (object);

	g_mutex_free (priv->property_lock);

	g_free (priv->host);
	g_free (priv->method);
	g_free (priv->user);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (e_source_authentication_parent_class)->finalize (object);
}

static void
e_source_authentication_class_init (ESourceAuthenticationClass *class)
{
	GObjectClass *object_class;
	ESourceExtensionClass *extension_class;

	g_type_class_add_private (class, sizeof (ESourceAuthenticationPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = source_authentication_set_property;
	object_class->get_property = source_authentication_get_property;
	object_class->finalize = source_authentication_finalize;

	extension_class = E_SOURCE_EXTENSION_CLASS (class);
	extension_class->name = E_SOURCE_EXTENSION_AUTHENTICATION;

	g_object_class_install_property (
		object_class,
		PROP_HOST,
		g_param_spec_string (
			"host",
			"Host",
			"Host name for the remote account",
			"",
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_STATIC_STRINGS |
			E_SOURCE_PARAM_SETTING));

	g_object_class_install_property (
		object_class,
		PROP_METHOD,
		g_param_spec_string (
			"method",
			"Method",
			"Authentication method",
			"none",
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_STATIC_STRINGS |
			E_SOURCE_PARAM_SETTING));

	g_object_class_install_property (
		object_class,
		PROP_PORT,
		g_param_spec_uint (
			"port",
			"Port",
			"Port number for the remote account",
			0, G_MAXUINT16, 0,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_STATIC_STRINGS |
			E_SOURCE_PARAM_SETTING));

	g_object_class_install_property (
		object_class,
		PROP_USER,
		g_param_spec_string (
			"user",
			"User",
			"User name for the remote account",
			NULL,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_STATIC_STRINGS |
			E_SOURCE_PARAM_SETTING));
}

static void
e_source_authentication_init (ESourceAuthentication *extension)
{
	extension->priv = E_SOURCE_AUTHENTICATION_GET_PRIVATE (extension);
	extension->priv->property_lock = g_mutex_new ();
}

/**
 * e_source_authentication_required:
 * @extension: an #ESourceAuthentication
 *
 * This is a convenience function which returns whether authentication
 * is required at all, regardless of the method used.  This relies on
 * the convention of setting #ESourceAuthentication:method to "none"
 * when authentication is <emphasis>not</emphasis> required.
 *
 * Returns: whether authentication is required at all
 *
 * Since: 3.6
 **/
gboolean
e_source_authentication_required (ESourceAuthentication *extension)
{
	const gchar *method;

	g_return_val_if_fail (E_IS_SOURCE_AUTHENTICATION (extension), FALSE);

	method = e_source_authentication_get_method (extension);
	g_return_val_if_fail (method != NULL && *method != '\0', FALSE);

	return (g_strcmp0 (method, "none") != 0);
}

/**
 * e_source_authentication_get_host:
 * @extension: an #ESourceAuthentication
 *
 * Returns the host name used to authenticate to a remote account.
 *
 * Returns: the host name of a remote account
 *
 * Since: 3.6
 **/
const gchar *
e_source_authentication_get_host (ESourceAuthentication *extension)
{
	g_return_val_if_fail (E_IS_SOURCE_AUTHENTICATION (extension), NULL);

	return extension->priv->host;
}

/**
 * e_source_authentication_dup_host:
 * @extension: an #ESourceAuthentication
 *
 * Thread-safe variation of e_source_authentication_get_host().
 * Use this function when accessing @extension from multiple threads.
 *
 * The returned string should be freed with g_free() when no longer needed.
 *
 * Returns: a newly-allocated copy of #ESourceAuthentication:host
 *
 * Since: 3.6
 **/
gchar *
e_source_authentication_dup_host (ESourceAuthentication *extension)
{
	const gchar *protected;
	gchar *duplicate;

	g_return_val_if_fail (E_IS_SOURCE_AUTHENTICATION (extension), NULL);

	g_mutex_lock (extension->priv->property_lock);

	protected = e_source_authentication_get_host (extension);
	duplicate = g_strdup (protected);

	g_mutex_unlock (extension->priv->property_lock);

	return duplicate;
}

/**
 * e_source_authentication_set_host:
 * @extension: an #ESourceAuthentication
 * @host: (allow-none): a host name, or %NULL
 *
 * Sets the host name used to authenticate to a remote account.
 *
 * The internal copy of @host is automatically stripped of leading and
 * trailing whitespace.  If the resulting string is empty, %NULL is set
 * instead.
 *
 * Since: 3.6
 **/
void
e_source_authentication_set_host (ESourceAuthentication *extension,
                                  const gchar *host)
{
	g_return_if_fail (E_IS_SOURCE_AUTHENTICATION (extension));

	g_mutex_lock (extension->priv->property_lock);

	if (g_strcmp0 (extension->priv->host, host) == 0) {
		g_mutex_unlock (extension->priv->property_lock);
		return;
	}

	g_free (extension->priv->host);
	extension->priv->host = e_util_strdup_strip (host);

	g_mutex_unlock (extension->priv->property_lock);

	g_object_notify (G_OBJECT (extension), "host");
}

/**
 * e_source_authentication_get_method:
 * @extension: an #ESourceAuthentication
 *
 * Returns the authentication method for a remote account.  There are
 * no pre-defined method names; backends are free to set this however
 * they wish.  If authentication is not required for a remote account,
 * the convention is to set #ESourceAuthentication:method to "none".
 *
 * Returns: the authentication method for a remote account
 *
 * Since: 3.6
 **/
const gchar *
e_source_authentication_get_method (ESourceAuthentication *extension)
{
	g_return_val_if_fail (E_IS_SOURCE_AUTHENTICATION (extension), NULL);

	return extension->priv->method;
}

/**
 * e_source_authentication_dup_method:
 * @extension: an #ESourceAuthentication
 *
 * Thread-safe variation of e_source_authentication_get_method().
 * Use this function when accessing @extension from multiple threads.
 *
 * The returned string should be freed with g_free() when no longer needed.
 *
 * Returns: a newly-allocated copy of #ESourceAuthentication:method
 *
 * Since: 3.6
 **/
gchar *
e_source_authentication_dup_method (ESourceAuthentication *extension)
{
	const gchar *protected;
	gchar *duplicate;

	g_return_val_if_fail (E_IS_SOURCE_AUTHENTICATION (extension), NULL);

	g_mutex_lock (extension->priv->property_lock);

	protected = e_source_authentication_get_method (extension);
	duplicate = g_strdup (protected);

	g_mutex_unlock (extension->priv->property_lock);

	return duplicate;
}

/**
 * e_source_authentication_set_method:
 * @extension: an #ESourceAuthentication
 * @method: (allow-none): authentication method, or %NULL
 *
 * Sets the authentication method for a remote account.  There are no
 * pre-defined method names; backends are free to set this however they
 * wish.  If authentication is not required for a remote account, the
 * convention is to set the method to "none".  In keeping with that
 * convention, #ESourceAuthentication:method will be set to "none" if
 * @method is %NULL or an empty string.
 *
 * Since: 3.6
 **/
void
e_source_authentication_set_method (ESourceAuthentication *extension,
                                    const gchar *method)
{
	g_return_if_fail (E_IS_SOURCE_AUTHENTICATION (extension));

	g_mutex_lock (extension->priv->property_lock);

	if (g_strcmp0 (extension->priv->method, method) == 0) {
		g_mutex_unlock (extension->priv->property_lock);
		return;
	}

	g_free (extension->priv->method);
	extension->priv->method = e_util_strdup_strip (method);

	if (extension->priv->method == NULL)
		extension->priv->method = g_strdup ("none");

	g_mutex_unlock (extension->priv->property_lock);

	g_object_notify (G_OBJECT (extension), "method");
}

/**
 * e_source_authentication_get_port:
 * @extension: an #ESourceAuthentication
 *
 * Returns the port number used to authenticate to a remote account.
 *
 * Returns: the port number of a remote account
 *
 * Since: 3.6
 **/
guint16
e_source_authentication_get_port (ESourceAuthentication *extension)
{
	g_return_val_if_fail (E_IS_SOURCE_AUTHENTICATION (extension), 0);

	return extension->priv->port;
}

/**
 * e_source_authentication_set_port:
 * @extension: an #ESourceAuthentication
 * @port: a port number
 *
 * Sets the port number used to authenticate to a remote account.
 *
 * Since: 3.6
 **/
void
e_source_authentication_set_port (ESourceAuthentication *extension,
                                  guint16 port)
{
	g_return_if_fail (E_SOURCE_AUTHENTICATION (extension));

	if (extension->priv->port == port)
		return;

	extension->priv->port = port;

	g_object_notify (G_OBJECT (extension), "port");
}

/**
 * e_source_authentication_get_user:
 * @extension: an #ESourceAuthentication
 *
 * Returns the user name used to authenticate to a remote account.
 *
 * Returns: the user name of a remote account
 *
 * Since: 3.6
 **/
const gchar *
e_source_authentication_get_user (ESourceAuthentication *extension)
{
	g_return_val_if_fail (E_IS_SOURCE_AUTHENTICATION (extension), NULL);

	return extension->priv->user;
}

/**
 * e_source_authentication_dup_user:
 * @extension: an #ESourceAuthentication
 *
 * Thread-safe variation of e_source_authentication_get_user().
 * Use this function when accessing @extension from multiple threads.
 *
 * The returned string should be freed with g_free() when no longer needed.
 *
 * Returns: a newly-allocated copy of #ESourceAuthentication:user
 *
 * Since: 3.6
 **/
gchar *
e_source_authentication_dup_user (ESourceAuthentication *extension)
{
	const gchar *protected;
	gchar *duplicate;

	g_return_val_if_fail (E_IS_SOURCE_AUTHENTICATION (extension), NULL);

	g_mutex_lock (extension->priv->property_lock);

	protected = e_source_authentication_get_user (extension);
	duplicate = g_strdup (protected);

	g_mutex_unlock (extension->priv->property_lock);

	return duplicate;
}

/**
 * e_source_authentication_set_user:
 * @extension: an #ESourceAuthentication
 * @user: (allow-none): a user name, or %NULL
 *
 * Sets the user name used to authenticate to a remote account.
 *
 * The internal copy of @user is automatically stripped of leading and
 * trailing whitespace.  If the resulting string is empty, %NULL is set
 * instead.
 *
 * Since: 3.6
 **/
void
e_source_authentication_set_user (ESourceAuthentication *extension,
                                  const gchar *user)
{
	g_return_if_fail (E_IS_SOURCE_AUTHENTICATION (extension));

	g_mutex_lock (extension->priv->property_lock);

	if (g_strcmp0 (extension->priv->user, user) == 0) {
		g_mutex_unlock (extension->priv->property_lock);
		return;
	}

	g_free (extension->priv->user);
	extension->priv->user = e_util_strdup_strip (user);

	g_mutex_unlock (extension->priv->property_lock);

	g_object_notify (G_OBJECT (extension), "user");
}
