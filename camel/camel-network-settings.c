/*
 * camel-network-settings.c
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

#include "camel-network-settings.h"

#include <camel/camel-enumtypes.h>
#include <camel/camel-settings.h>

#define SECURITY_METHOD_KEY "CamelNetworkSettings:security-method"

G_DEFINE_INTERFACE (
	CamelNetworkSettings,
	camel_network_settings,
	CAMEL_TYPE_SETTINGS)

static void
camel_network_settings_default_init (CamelNetworkSettingsInterface *interface)
{
	g_object_interface_install_property (
		interface,
		g_param_spec_enum (
			"security-method",
			"Security Method",
			"Method used to establish a network connection",
			CAMEL_TYPE_NETWORK_SECURITY_METHOD,
			CAMEL_NETWORK_SECURITY_METHOD_STARTTLS_ON_STANDARD_PORT,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT |
			G_PARAM_STATIC_STRINGS));
}

/**
 * camel_network_settings_get_security_method:
 * @settings: a #CamelNetworkSettings
 *
 * Returns the method used to establish a secure (or unsecure) network
 * connection.
 *
 * Returns: the security method
 *
 * Since: 3.2
 **/
CamelNetworkSecurityMethod
camel_network_settings_get_security_method (CamelNetworkSettings *settings)
{
	gpointer data;

	g_return_val_if_fail (
		CAMEL_IS_NETWORK_SETTINGS (settings),
		CAMEL_NETWORK_SECURITY_METHOD_NONE);

	data = g_object_get_data (G_OBJECT (settings), SECURITY_METHOD_KEY);

	return (CamelNetworkSecurityMethod) GPOINTER_TO_INT (data);
}

/**
 * camel_network_settings_set_security_method:
 * @settings: a #CamelNetworkSettings
 * @method: the security method
 *
 * Sets the method used to establish a secure (or unsecure) network
 * connection.  Note that changing this setting has no effect on an
 * already-established network connection.
 *
 * Since: 3.2
 **/
void
camel_network_settings_set_security_method (CamelNetworkSettings *settings,
                                            CamelNetworkSecurityMethod method)
{
	g_return_if_fail (CAMEL_IS_NETWORK_SETTINGS (settings));

	g_object_set_data (
		G_OBJECT (settings),
		SECURITY_METHOD_KEY,
		GINT_TO_POINTER (method));

	g_object_notify (G_OBJECT (settings), "security-method");
}
