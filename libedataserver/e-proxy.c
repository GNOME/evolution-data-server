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
 *		Jeffrey Stedfast <fejj@ximian.com>
 *		Veerapuram Varadhan <vvaradhan@novell.com>
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <stdlib.h>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <netinet/in.h>
#include <sys/socket.h>
#endif

#include <glib.h>
#include <glib-object.h>

#include <gconf/gconf.h>
#include <gconf/gconf-client.h>

#include <libsoup/soup-address.h>
#include <libsoup/soup-uri.h>
#include "e-proxy.h"

/* Debug */
#define d(x)

enum ProxyType {
	PROXY_TYPE_SYSTEM = 0,
	PROXY_TYPE_NO_PROXY,
	PROXY_TYPE_MANUAL,
	PROXY_TYPE_AUTO_URL /* no auto-proxy at the moment */
};

/* GConf paths and keys */
#define PATH_GCONF_EVO_NETWORK_CONFIG "/apps/evolution/shell/network_config"
#define KEY_GCONF_EVO_PROXY_TYPE       PATH_GCONF_EVO_NETWORK_CONFIG "/proxy_type"

#define KEY_GCONF_EVO_USE_HTTP_PROXY    PATH_GCONF_EVO_NETWORK_CONFIG "/use_http_proxy"
#define KEY_GCONF_EVO_HTTP_HOST         PATH_GCONF_EVO_NETWORK_CONFIG "/http_host"
#define KEY_GCONF_EVO_HTTP_PORT         PATH_GCONF_EVO_NETWORK_CONFIG "/http_port"
#define KEY_GCONF_EVO_HTTP_USE_AUTH     PATH_GCONF_EVO_NETWORK_CONFIG "/use_authentication"
#define KEY_GCONF_EVO_HTTP_AUTH_USER    PATH_GCONF_EVO_NETWORK_CONFIG "/authentication_user"
#define KEY_GCONF_EVO_HTTP_AUTH_PWD     PATH_GCONF_EVO_NETWORK_CONFIG "/authentication_password"
#define KEY_GCONF_EVO_HTTP_IGNORE_HOSTS PATH_GCONF_EVO_NETWORK_CONFIG "/ignore_hosts"
#define KEY_GCONF_EVO_HTTPS_HOST        PATH_GCONF_EVO_NETWORK_CONFIG "/secure_host"
#define KEY_GCONF_EVO_HTTPS_PORT        PATH_GCONF_EVO_NETWORK_CONFIG "/secure_port"
#define KEY_GCONF_EVO_SOCKS_HOST        PATH_GCONF_EVO_NETWORK_CONFIG "/socks_host"
#define KEY_GCONF_EVO_SOCKS_PORT        PATH_GCONF_EVO_NETWORK_CONFIG "/socks_port"
#define KEY_GCONF_EVO_AUTOCONFIG_URL    PATH_GCONF_EVO_NETWORK_CONFIG "/autoconfig_url"

#define PATH_GCONF_SYS_PROXY "/system/proxy"
#define PATH_GCONF_SYS_HTTP_PROXY "/system/http_proxy"

#define KEY_GCONF_SYS_USE_HTTP_PROXY    PATH_GCONF_SYS_HTTP_PROXY "/use_http_proxy"
#define KEY_GCONF_SYS_HTTP_HOST         PATH_GCONF_SYS_HTTP_PROXY "/host"
#define KEY_GCONF_SYS_HTTP_PORT         PATH_GCONF_SYS_HTTP_PROXY "/port"
#define KEY_GCONF_SYS_HTTP_USE_AUTH     PATH_GCONF_SYS_HTTP_PROXY "/use_authentication"
#define KEY_GCONF_SYS_HTTP_AUTH_USER    PATH_GCONF_SYS_HTTP_PROXY "/authentication_user"
#define KEY_GCONF_SYS_HTTP_AUTH_PWD     PATH_GCONF_SYS_HTTP_PROXY "/authentication_password"
#define KEY_GCONF_SYS_HTTP_IGNORE_HOSTS PATH_GCONF_SYS_HTTP_PROXY "/ignore_hosts"
#define KEY_GCONF_SYS_HTTPS_HOST        PATH_GCONF_SYS_PROXY "/secure_host"
#define KEY_GCONF_SYS_HTTPS_PORT        PATH_GCONF_SYS_PROXY "/secure_port"
#define KEY_GCONF_SYS_SOCKS_HOST        PATH_GCONF_SYS_PROXY "/socks_host"
#define KEY_GCONF_SYS_SOCKS_PORT        PATH_GCONF_SYS_PROXY "/socks_port"
#define KEY_GCONF_SYS_AUTOCONFIG_URL    PATH_GCONF_SYS_PROXY "/autoconfig_url"

#define RIGHT_KEY(sufix) (priv->type == PROXY_TYPE_SYSTEM ? KEY_GCONF_SYS_ ## sufix : KEY_GCONF_EVO_ ## sufix)

struct _EProxyPrivate {
	SoupURI *uri_http, *uri_https;
	guint notify_id_evo, notify_id_sys, notify_id_sys_http; /* conxn id of gconf_client_notify_add  */
	GSList* ign_hosts;	/* List of hostnames. (Strings)		*/
	GSList* ign_addrs;	/* List of hostaddrs. (ProxyHostAddrs)	*/
	gboolean use_proxy;	/* Is our-proxy enabled? */
	enum ProxyType type;
};

/* Enum definition is copied from gnome-vfs/modules/http-proxy.c */
typedef enum {
	PROXY_IPV4 = 4,
	PROXY_IPV6 = 6
} ProxyAddrType;

typedef struct {
	ProxyAddrType type;	/* Specifies whether IPV4 or IPV6 */
	gpointer  addr;		/* Either in_addr* or in6_addr* */
	gpointer  mask;		/* Either in_addr* or in6_addr* */
} ProxyHostAddr;

/* Signals.  */
enum {
	CHANGED,
	LAST_SIGNAL
};

static GObjectClass *parent_class;
static guint signals[LAST_SIGNAL] = { 0 };

/* Forward declarations.  */

static void	ep_setting_changed	(GConfClient *client, guint32 cnxn_id,
					 GConfEntry *entry, gpointer user_data);
static void	e_proxy_dispose	(GObject* object);

static void	ipv6_network_addr	(const struct in6_addr *addr,
					 const struct in6_addr *mask,
					 struct in6_addr *res);

static void
ep_free_proxy_host_addr (ProxyHostAddr* host)
{
	if (host) {
		if (host->addr) {
			g_free (host->addr);
			host->addr = NULL;
		}
		if (host->mask) {
			g_free (host->mask);
			host->mask = NULL;
		}
		g_free (host);
	}
}

static void
e_proxy_class_init (EProxyClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->dispose = e_proxy_dispose;

        /* signals */

        /**
         * EProxy::changed:
         * @proxy: the proxy
         *
         * Emitted when proxy settings in gconf changes.
         **/
        signals[CHANGED] =
                g_signal_new ("changed",
                              G_OBJECT_CLASS_TYPE (object_class),
                              G_SIGNAL_RUN_FIRST,
                              G_STRUCT_OFFSET (EProxyClass, changed),
                              NULL, NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE, 0);

}

static void
e_proxy_init (EProxy *pxy, EProxyClass *klass)
{
	EProxyPrivate *priv;

	/* allocate internal structure */
	priv = g_new0 (EProxyPrivate, 1);
	pxy->priv = priv;
	priv->ign_hosts = NULL;
	priv->ign_addrs = NULL;
	priv->uri_http = NULL;
	priv->uri_https = NULL;
	priv->notify_id_evo = 0;
	priv->notify_id_sys = 0;
	priv->notify_id_sys_http = 0;
	priv->use_proxy = FALSE;
	priv->type = PROXY_TYPE_SYSTEM;
}

static void
e_proxy_dispose (GObject *object)
{
	EProxy *proxy = (EProxy *)object;
	EProxyPrivate *priv;

	if (!E_IS_PROXY (proxy))
		return;

	priv = proxy->priv;

	if (priv) {
		GConfClient* client = NULL;

		if ((client = gconf_client_get_default ())) {
			if (priv->notify_id_evo > 0)
				gconf_client_notify_remove (client, priv->notify_id_evo);
			if (priv->notify_id_sys > 0)
				gconf_client_notify_remove (client, priv->notify_id_sys);
			if (priv->notify_id_sys_http > 0)
				gconf_client_notify_remove (client, priv->notify_id_sys_http);
			g_object_unref (client);
		}

		if (priv->uri_http)
			soup_uri_free (priv->uri_http);
		if (priv->uri_https)
			soup_uri_free (priv->uri_https);

		if (priv->ign_hosts) {
			g_slist_foreach (priv->ign_hosts, (GFunc) g_free, NULL);
			g_slist_free (priv->ign_hosts);
		}

		if (priv->ign_addrs) {
			g_slist_foreach (priv->ign_addrs, (GFunc) ep_free_proxy_host_addr, NULL);
			g_slist_free (priv->ign_addrs);
		}

		priv->notify_id_evo = 0;
		priv->notify_id_sys = 0;
		priv->notify_id_sys_http = 0;

		g_free (priv);
		priv = NULL;
	}
}

GType
e_proxy_get_type (void)
{
	static GType type = 0;

	if (!type) {
		static GTypeInfo info = {
                        sizeof (EProxyClass),
                        (GBaseInitFunc) NULL,
                        (GBaseFinalizeFunc) NULL,
                        (GClassInitFunc) e_proxy_class_init,
                        NULL, NULL,
                        sizeof (EProxy),
                        0,
                        (GInstanceInitFunc) e_proxy_init
                };
		type = g_type_register_static (G_TYPE_OBJECT, "EProxy", &info, 0);
	}

	return type;
}

static gboolean
ep_is_in_ignored (EProxy *proxy, const gchar *host)
{
	EProxyPrivate *priv;
	GSList* l;
	gchar *hn;

	g_return_val_if_fail (proxy != NULL, FALSE);
	g_return_val_if_fail (host != NULL, FALSE);

	priv = proxy->priv;
	if (!priv->ign_hosts)
		return FALSE;

	hn = g_ascii_strdown (host, -1);

	for (l = priv->ign_hosts; l; l = l->next) {
		if (*((gchar *)l->data) == '*') {
			if (g_str_has_suffix (hn, ((gchar *)l->data)+1)) {
				g_free (hn);
				return TRUE;
			}
		} else if (strcmp (hn, l->data) == 0) {
				g_free (hn);
				return TRUE;
		}
	}
	g_free (hn);

	return FALSE;
}

static gboolean
ep_need_proxy_http (EProxy* proxy, const gchar * host)
{
	SoupAddress *addr = NULL;
	EProxyPrivate *priv = proxy->priv;
	ProxyHostAddr *p_addr = NULL;
	GSList *l;
	guint status;

	/* check for ignored first */
	if (ep_is_in_ignored (proxy, host))
		return FALSE;

	addr = soup_address_new (host, 0);
	status = soup_address_resolve_sync (addr, NULL);
	if (status == SOUP_STATUS_OK) {
		gint addr_len;
		struct sockaddr* so_addr = NULL;

		so_addr = soup_address_get_sockaddr (addr, &addr_len);

		/* This will never happen, since we have already called
		   soup_address_resolve_sync().
		*/
		if (!so_addr)
			return TRUE;

		if (so_addr->sa_family == AF_INET) {
			struct in_addr in, *mask, *addr_in;

			in = ((struct sockaddr_in *)so_addr)->sin_addr;
			for (l = priv->ign_addrs; l; l = l->next) {
				p_addr = (ProxyHostAddr *)l->data;
				if (p_addr->type == PROXY_IPV4) {
					addr_in =  ((struct in_addr *)p_addr->addr);
					mask = ((struct in_addr *)p_addr->mask);
					d(g_print ("ep_need_proxy:ipv4: in: %ul\t mask: %ul\t addr: %ul\n",
						   in.s_addr, mask->s_addr, addr_in->s_addr));
					if ((in.s_addr & mask->s_addr) == addr_in->s_addr) {
						d(g_print ("Host [%s] doesn't require proxy\n", host));
						return FALSE;
					}
				}
			}
		} else {
			struct in6_addr in6, net6;
			struct in_addr *addr_in, *mask;

			in6 = ((struct sockaddr_in6 *)so_addr)->sin6_addr;
			for (l = priv->ign_addrs; l; l = l->next) {
				p_addr = (ProxyHostAddr *)l->data;
				ipv6_network_addr (&in6, (struct in6_addr *)p_addr->mask, &net6);
				if (p_addr->type == PROXY_IPV6) {
					if (IN6_ARE_ADDR_EQUAL (&net6, (struct in6_addr *)p_addr->addr)) {
						d(g_print ("Host [%s] doesn't require proxy\n", host));
						return FALSE;
					}
				} else if (p_addr->type == PROXY_IPV6 &&
					   IN6_IS_ADDR_V4MAPPED (&net6)) {
					guint32 v4addr;

					addr_in =  ((struct in_addr *)p_addr->addr);
					mask = ((struct in_addr *)p_addr->mask);

					v4addr = net6.s6_addr[12] << 24
						| net6.s6_addr[13] << 16
						| net6.s6_addr[14] << 8
						| net6.s6_addr[15];
					if ((v4addr & mask->s_addr) != addr_in->s_addr) {
						d(g_print ("Host [%s] doesn't require proxy\n", host));
						return FALSE;
					}
				}
			}
		}
	}

	d(g_print ("%s needs a proxy to connect to internet\n", host));
	return TRUE;
}

static gboolean
ep_need_proxy_https (EProxy* proxy, const gchar * host)
{
	/* Can we share ignore list from HTTP at all? */
	return !ep_is_in_ignored (proxy, host);
}

static gboolean
ep_manipulate_ipv4 (ProxyHostAddr *host_addr,
		    struct in_addr *addr_in,
		    gchar * netmask)
{
	gboolean has_error = FALSE;
	struct in_addr *addr, *mask;

	if (!addr_in)
		return has_error;

	host_addr->type = PROXY_IPV4;
	addr = g_new0 (struct in_addr, 1);
	memcpy (addr, addr_in, sizeof (struct in_addr));
	mask = g_new0 (struct in_addr, 1);

	if (netmask) {
		gchar *endptr;
		gint width = strtol (netmask, &endptr, 10);

		if (*endptr != '\0' || width < 0 || width > 32) {
			has_error = TRUE;
		}
		mask->s_addr = htonl (~0 << width);
		addr->s_addr &= mask->s_addr;
	} else {
		mask->s_addr = 0xFFFFFFFF;
	}

	d(g_print ("ep_manipulate_ipv4: addr_in: %ul, addr: %ul, mask: %ul\n",
		   addr_in->s_addr, addr->s_addr, mask->s_addr));

	host_addr->addr = addr;
	host_addr->mask = mask;

	return has_error;
}

static void
ipv6_network_addr(const struct in6_addr *addr, const struct in6_addr *mask,
                  struct in6_addr *res)
{
	gint i;

	for (i = 0; i < 16; ++i) {
		res->s6_addr[i] = addr->s6_addr[i] & mask->s6_addr[i];
	}
}

static gboolean
ep_manipulate_ipv6 (ProxyHostAddr *host_addr,
		    struct in6_addr *addr_in6,
		    gchar * netmask)
{
	gboolean has_error = FALSE;
	struct in6_addr *addr, *mask;
	gint i;

	if (!addr_in6)
		return has_error;

	host_addr->type = PROXY_IPV6;

	addr = g_new0 (struct in6_addr, 1);
	mask = g_new0 (struct in6_addr, 1);

        for (i = 0; i < 16; ++i) {
		addr->s6_addr[i] = addr_in6->s6_addr[i];
        }
        if (netmask) {
		gchar *endptr;
		gint width = strtol(netmask, &endptr, 10);

		if (*endptr != '\0' || width < 0 || width > 128) {
			has_error = TRUE;
		}
		for (i = 0; i < 16; ++i) {
			mask->s6_addr[i] = 0;
		}
		for (i = 0; i < width / 8; i++) {
			mask->s6_addr[i] = 0xff;
		}
		mask->s6_addr[i] = (0xff << (8 - width % 8)) & 0xff;
		ipv6_network_addr(addr, mask, addr);
        } else {
		for (i = 0; i < 16; ++i) {
			mask->s6_addr[i] = 0xff;
		}
        }

	host_addr->addr = addr;
	host_addr->mask = mask;

	return has_error;
}

static void
ep_parse_ignore_host (gpointer data, gpointer user_data)
{
	EProxy* proxy = (EProxy *)user_data;
	EProxyPrivate* priv = NULL;
	SoupAddress *addr;
	guint status;
	gchar *input, *netmask, *hostname;
	ProxyHostAddr *host_addr;
	gboolean has_error = FALSE;

	if (!proxy || !proxy->priv)
		return;

	priv = proxy->priv;
	input = (gchar *)data;

	if ((netmask = strrchr (input, '/')) != NULL) {
		hostname = g_strndup (input, netmask - input);
		++netmask;
	} else {
		hostname = g_ascii_strdown (input, -1);
	}

	addr = soup_address_new (hostname, 0);
	status = soup_address_resolve_sync (addr, NULL);
	if (status == SOUP_STATUS_OK) {
		gint addr_len;
		struct sockaddr* so_addr = NULL;

		host_addr = g_new0 (ProxyHostAddr, 1);

		so_addr = soup_address_get_sockaddr (addr, &addr_len);

		/* This will never happen, since we have already called
		   soup_address_resolve_sync().
		*/
		if (!so_addr)
			goto error;

		if (so_addr->sa_family == AF_INET)
			has_error = ep_manipulate_ipv4 (host_addr,
							&((struct sockaddr_in *)so_addr)->sin_addr,
							netmask);
		else
			has_error = ep_manipulate_ipv6 (host_addr,
							&((struct sockaddr_in6 *)so_addr)->sin6_addr,
							netmask);

		if (!has_error)
			priv->ign_addrs = g_slist_append (priv->ign_addrs, host_addr);

		g_free (hostname);
	} else {
		d(g_print ("Unable to resolve %s\n", hostname));
		priv->ign_hosts = g_slist_append (priv->ign_hosts, hostname);
	}
 error:
	g_object_unref (addr);
}

static gboolean
ep_change_uri (SoupURI **soup_uri, const gchar *uri)
{
	gboolean changed = FALSE;

	g_return_val_if_fail (soup_uri != NULL, FALSE);

	if (!uri || !*uri) {
		if (*soup_uri) {
			soup_uri_free (*soup_uri);
			*soup_uri = NULL;
			changed = TRUE;
		}
	} else if (*soup_uri) {
		gchar *old = soup_uri_to_string (*soup_uri, FALSE);

		changed = old && uri && g_ascii_strcasecmp (old, uri) != 0;
		if (changed) {
			soup_uri_free (*soup_uri);
			*soup_uri = soup_uri_new (uri);
		}

		g_free (old);
	} else {
		*soup_uri = soup_uri_new (uri);
		changed = TRUE;
	}

	return changed;
}

static void
ep_set_proxy (GConfClient *client,
	      gpointer user_data,
	      gboolean regen_ign_host_list)
{
	gchar *proxy_server, *uri_http = NULL, *uri_https = NULL;
	gint proxy_port, old_type;
	EProxy* proxy = (EProxy *)user_data;
	EProxyPrivate* priv = proxy->priv;
	GSList *ignore;
	gboolean changed = FALSE;

	old_type = priv->type;
	priv->type = gconf_client_get_int (client, KEY_GCONF_EVO_PROXY_TYPE, NULL);
	if (priv->type > PROXY_TYPE_AUTO_URL)
		priv->type = PROXY_TYPE_SYSTEM;
	changed = priv->type != old_type;

	priv->use_proxy = gconf_client_get_bool (client, RIGHT_KEY (USE_HTTP_PROXY), NULL);
	if (!priv->use_proxy || priv->type == PROXY_TYPE_NO_PROXY) {
		changed = ep_change_uri (&priv->uri_http, NULL) || changed;
		changed = ep_change_uri (&priv->uri_https, NULL) || changed;
		goto emit_signal;
	}

	proxy_server = gconf_client_get_string (client, RIGHT_KEY (HTTP_HOST), NULL);
	proxy_port = gconf_client_get_int (client, RIGHT_KEY (HTTP_PORT), NULL);
	if (proxy_server != NULL)
		uri_http = g_strdup_printf (
			"http://%s:%d", proxy_server, proxy_port);
	else
		uri_http = NULL;
	g_free (proxy_server);
	d(g_print ("ep_set_proxy: uri_http: %s\n", uri_http));

	proxy_server = gconf_client_get_string (client, RIGHT_KEY (HTTPS_HOST), NULL);
	proxy_port = gconf_client_get_int (client, RIGHT_KEY (HTTPS_PORT), NULL);
	if (proxy_server != NULL)
		uri_https = g_strdup_printf (
			"https://%s:%d", proxy_server, proxy_port);
	else
		uri_https = NULL;
	g_free (proxy_server);
	d(g_print ("ep_set_proxy: uri_http: %s\n", uri_http));

	if (regen_ign_host_list) {
		if (priv->ign_hosts) {
			g_slist_foreach (priv->ign_hosts, (GFunc) g_free, NULL);
			g_slist_free (priv->ign_hosts);
			priv->ign_hosts = NULL;
		}

		if (priv->ign_addrs) {
			g_slist_foreach (priv->ign_addrs, (GFunc) ep_free_proxy_host_addr, NULL);
			g_slist_free (priv->ign_addrs);
			priv->ign_addrs = NULL;
		}

		ignore = gconf_client_get_list (client, RIGHT_KEY (HTTP_IGNORE_HOSTS), GCONF_VALUE_STRING, NULL);
		if (ignore) {
			g_slist_foreach (ignore, (GFunc) ep_parse_ignore_host, proxy);
			g_slist_foreach (ignore, (GFunc) g_free, NULL);
			g_slist_free (ignore);
		}
	}

	if (gconf_client_get_bool (client, RIGHT_KEY (HTTP_USE_AUTH), NULL)) {
		gchar *proxy_user, *proxy_pw, *tmp = NULL;

		proxy_user = gconf_client_get_string (client, RIGHT_KEY (HTTP_AUTH_USER), NULL);
		proxy_pw = gconf_client_get_string (client, RIGHT_KEY (HTTP_AUTH_PWD), NULL);

		if (proxy_user && *proxy_user && proxy_pw && *proxy_pw) {
			tmp = uri_http;
			uri_http = g_strdup_printf ("http://%s:%s@%s", proxy_user, proxy_pw, tmp + strlen ("http://"));
		} else if (proxy_user && *proxy_user) {
			/* proxy without password, just try it */
			tmp = uri_http;
			uri_http = g_strdup_printf ("http://%s@%s", proxy_user, tmp + strlen ("http://"));
		}

		g_free (proxy_user);
		g_free (proxy_pw);
		g_free (tmp);
	}

	changed = ep_change_uri (&priv->uri_http, uri_http) || changed;
	changed = ep_change_uri (&priv->uri_https, uri_https) || changed;
	d(g_print ("system-proxy: uri_http: %s; uri_https: %s\n", uri_http ? uri_http : "[null]", uri_https ? uri_https : "[null]"));

 emit_signal:
	if (changed)
		g_signal_emit (proxy, signals[CHANGED], 0);

	g_free (uri_http);
	g_free (uri_https);

	return;
}

static void
ep_setting_changed (GConfClient *client, guint32 cnxn_id, GConfEntry *entry, gpointer user_data)
{
	const gchar *key;
	EProxy* proxy = (EProxy *)user_data;
	EProxyPrivate *priv;

	if (!proxy || !proxy->priv)
		return;

	priv = proxy->priv;
	key = gconf_entry_get_key (entry);

	if (g_str_equal (key, KEY_GCONF_EVO_PROXY_TYPE)) {
		ep_set_proxy (client, user_data, FALSE);
		d(g_print ("e-proxy.c:ep_settings_changed: proxy type changed\n"));
	} else if (g_str_equal (key, RIGHT_KEY (USE_HTTP_PROXY)) ||
		   g_str_equal (key, RIGHT_KEY (HTTP_IGNORE_HOSTS)) ||
		   g_str_equal (key, RIGHT_KEY (HTTP_HOST)) ||
		   g_str_equal (key, RIGHT_KEY (HTTP_PORT))) {
		gboolean regen_ign_host_list = FALSE;

		if (g_str_equal (key, RIGHT_KEY (HTTP_IGNORE_HOSTS)))
			regen_ign_host_list = TRUE;

		ep_set_proxy (client, user_data, regen_ign_host_list);
		d(g_print ("e-proxy.c:ep_settings_changed: proxy settings changed\n"));
	} else if (g_str_equal (key, RIGHT_KEY (HTTP_AUTH_USER)) ||
		   g_str_equal (key, RIGHT_KEY (HTTP_AUTH_PWD)) ||
		   g_str_equal (key, RIGHT_KEY (HTTP_USE_AUTH))) {

		ep_set_proxy (client, user_data, FALSE);
		d(g_print ("e-proxy.c:ep_settings_changed: auth settings changed\n"));
	} else if (g_str_equal (key, RIGHT_KEY (HTTPS_HOST)) ||
		   g_str_equal (key, RIGHT_KEY (HTTPS_PORT))) {

		ep_set_proxy (client, user_data, FALSE);
		d(g_print ("e-proxy.c:ep_settings_changed: https\n"));
	} else if (g_str_equal (key, RIGHT_KEY (SOCKS_HOST)) ||
		   g_str_equal (key, RIGHT_KEY (SOCKS_PORT)) ||
		   g_str_equal (key, RIGHT_KEY (AUTOCONFIG_URL))) {

		/* ep_set_proxy (client, user_data, FALSE); */
		d(g_print ("e-proxy.c:ep_settings_changed: socks/autoconf-url changed\n"));
	}
}

EProxy*
e_proxy_new (void)
{
	EProxy *proxy = NULL;

	proxy = g_object_new (E_TYPE_PROXY, NULL);

	return proxy;
}

void
e_proxy_setup_proxy (EProxy* proxy)
{
	GConfClient *client;

	/* We get the evolution-shell proxy keys here
	   set soup up to use the proxy,
	   and listen to any changes */

	if (!(client = gconf_client_get_default ()))
		return;

	if (!proxy || !proxy->priv)
		return;

	if (proxy->priv->notify_id_evo == 0) {
		/* Listen to the changes in the evolution-shell path */
		gconf_client_add_dir (client, PATH_GCONF_EVO_NETWORK_CONFIG, GCONF_CLIENT_PRELOAD_ONELEVEL, NULL);
		/* and system proxy setup changes */
		gconf_client_add_dir (client, PATH_GCONF_SYS_PROXY, GCONF_CLIENT_PRELOAD_ONELEVEL, NULL);
		gconf_client_add_dir (client, PATH_GCONF_SYS_HTTP_PROXY, GCONF_CLIENT_PRELOAD_ONELEVEL, NULL);

		proxy->priv->notify_id_evo = gconf_client_notify_add (client, PATH_GCONF_EVO_NETWORK_CONFIG,
								      ep_setting_changed, (gpointer)proxy,
								      NULL, NULL);
		proxy->priv->notify_id_sys = gconf_client_notify_add (client, PATH_GCONF_SYS_PROXY,
								      ep_setting_changed, (gpointer)proxy,
								      NULL, NULL);
		proxy->priv->notify_id_sys_http = gconf_client_notify_add (client, PATH_GCONF_SYS_HTTP_PROXY,
								      ep_setting_changed, (gpointer)proxy,
								      NULL, NULL);
	}

	ep_set_proxy (client, proxy, TRUE);

	g_object_unref (client);
}

SoupURI*
e_proxy_peek_uri_for (EProxy* proxy, const gchar *uri)
{
	SoupURI *suri;

	if (!proxy || !proxy->priv || !uri || !*uri)
		return NULL;

	suri = soup_uri_new (uri);
	g_return_val_if_fail (suri != NULL, NULL);

	if (suri->scheme == SOUP_URI_SCHEME_HTTPS) {
		return proxy->priv->uri_https;
	}

	return proxy->priv->uri_http;
}

gboolean
e_proxy_require_proxy_for_uri (EProxy* proxy, const gchar * uri)
{
	SoupURI *srv_uri = NULL;
	gboolean ret = FALSE;

	if (!uri || !proxy || !proxy->priv)
		return ret;

	if (!proxy->priv->use_proxy || proxy->priv->type == PROXY_TYPE_NO_PROXY) {
		d(g_print ("[%s] don't need a proxy to connect to internet\n", uri));
		return ret;
	}

	srv_uri = soup_uri_new (uri);

	if (srv_uri) {
		if (srv_uri->scheme == SOUP_URI_SCHEME_HTTPS) {
			ret = ep_need_proxy_https (proxy, srv_uri->host);
		} else {
			ret = ep_need_proxy_http (proxy, srv_uri->host);
		}

		soup_uri_free (srv_uri);
	}

	return ret;
}
