/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Authors: Jeffrey Stedfast <fejj@ximian.com>
 *	     Veerapuram Varadhan <vvaradhan@novell.com>
 *
 *  Copyright 2002 Ximian, Inc. (www.ximian.com)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Street #330, Boston, MA 02111-1307, USA.
 *
 */


#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <netinet/in.h>
#include <sys/socket.h>
#include <string.h>
#include <stdlib.h>
#include <glib.h>
#include <glib-object.h>

#include <gconf/gconf.h>
#include <gconf/gconf-client.h>

#include <libsoup/soup-address.h>
#include <libsoup/soup-uri.h>
#include "e-proxy.h"

/* Debug */
#define d(x)

/* GConf paths and keys */
#define PATH_GCONF_NETWORK_CONFIG "/apps/evolution/shell/network_config"
#define KEY_GCONF_HTTP_PROXY_PORT (PATH_GCONF_NETWORK_CONFIG "/" "http_port")
#define KEY_GCONF_HTTP_PROXY_HOST (PATH_GCONF_NETWORK_CONFIG "/" "http_host")
#define KEY_GCONF_HTTPS_PROXY_PORT (PATH_GCONF_NETWORK_CONFIG "/" "secure_port")
#define KEY_GCONF_HTTPS_PROXY_HOST (PATH_GCONF_NETWORK_CONFIG "/" "secure_host")
#define KEY_GCONF_SOCKS_PROXY_PORT (PATH_GCONF_NETWORK_CONFIG "/" "socks_port")
#define KEY_GCONF_SOCKS_PROXY_HOST (PATH_GCONF_NETWORK_CONFIG "/" "socks_host")
#define KEY_GCONF_USE_HTTP_PROXY (PATH_GCONF_NETWORK_CONFIG "/" "use_http_proxy")
#define KEY_GCONF_HTTP_AUTH_USER (PATH_GCONF_NETWORK_CONFIG "/" "authentication_user")
#define KEY_GCONF_HTTP_AUTH_PW   (PATH_GCONF_NETWORK_CONFIG "/" "authentication_password")
#define KEY_GCONF_HTTP_USE_AUTH (PATH_GCONF_NETWORK_CONFIG "/" "use_authentication")

#define KEY_GCONF_HTTP_PROXY_IGNORE_HOSTS (PATH_GCONF_NETWORK_CONFIG "/" "ignore_hosts")

struct _EProxyPrivate {
	SoupURI* uri;
	guint notify_id; 	/* conxn id of gconf_client_notify_add  */
	GSList* ign_hosts;	/* List of hostnames. (Strings)		*/
	GSList* ign_addrs;	/* List of hostaddrs. (ProxyHostAddrs)	*/
	gboolean use_proxy;	/* Is system-proxy enabled? */
};

/* Enum definition is copied from gnome-vfs/modules/http-proxy.c */
typedef enum {
	PROXY_IPV4 = 4,
	PROXY_IPV6 = 6
} ProxyAddrType;

typedef struct {
	ProxyAddrType type;	/* Specifies whether IPV4 or IPV6 */
	void* addr;		/* Either in_addr* or in6_addr* */
	void* mask;		/* Either in_addr* or in6_addr* */
} ProxyHostAddr;

/* Signals.  */
enum {
	CHANGED,
	LAST_SIGNAL
};

static GObjectClass *parent_class;
static unsigned int signals[LAST_SIGNAL] = { 0 };

/* Forward declarations.  */

static void 	ep_setting_changed 	(GConfClient *client, guint32 cnxn_id,
					 GConfEntry *entry, gpointer user_data);
static void 	e_proxy_dispose 	(GObject* object);

static void 	ipv6_network_addr	(const struct in6_addr *addr, 
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
	priv->uri = NULL;
	priv->notify_id = 0;
	priv->use_proxy = FALSE;
}

static void
e_proxy_dispose (GObject *object)
{
	EProxy *proxy = (EProxy *)object;
	
	if (!E_IS_PROXY (proxy))
		return;

	EProxyPrivate *priv = proxy->priv;

	if (priv) {
		GConfClient* client = NULL;

		if ((client = gconf_client_get_default ())) {
			if (priv->notify_id > 0)
				gconf_client_notify_remove (client, priv->notify_id);
			g_object_unref (client);
		}

		if (priv->uri)
			soup_uri_free (priv->uri);

		if (priv->ign_hosts) {
			g_slist_foreach (priv->ign_hosts, (GFunc) ep_free_proxy_host_addr, NULL);
			g_slist_free (priv->ign_hosts);
		}

		if (priv->ign_addrs) {
			g_slist_foreach (priv->ign_addrs, (GFunc) g_free, NULL);
			g_slist_free (priv->ign_addrs);
		}
		
		priv->notify_id = 0;
		
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
ep_need_proxy (EProxy* proxy, const char* host)
{
	SoupAddress *addr = NULL;
	EProxyPrivate *priv = proxy->priv;
	ProxyHostAddr *p_addr = NULL;
	GSList *l;
	guint status;

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
	} else {
		GSList* l;
		gchar* hn = g_ascii_strdown (host, -1);
		
		for (l = priv->ign_hosts; l; l = l->next) {
			if (*((gchar *)l->data) == '*') {
				if (g_str_has_suffix (hn, ((gchar *)l->data)+1)) {
					g_free (hn);
					return FALSE;
				}
			} else if (strcmp (hn, l->data) == 0) {
					g_free (hn);
					return FALSE;
			}
		}
		g_free (hn);
	}

	d(g_print ("%s needs a proxy to connect to internet\n", host));
	return TRUE;
}

static gboolean
ep_manipulate_ipv4 (ProxyHostAddr *host_addr, 
		    struct in_addr *addr_in, 
		    gchar* netmask)
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
		    gchar* netmask)
{
	gboolean has_error = FALSE;
	struct in6_addr *addr, *mask;
	int i;
	
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

static void
ep_set_proxy (GConfClient *client, 
	      gpointer user_data, 
	      gboolean regen_ign_host_list,
	      gboolean set_auth)
{
	char *proxy_server = NULL, *proxy_user = NULL, *proxy_pw = NULL, *uri = NULL;
	gboolean use_auth;
	int proxy_port;
	EProxy* proxy = (EProxy *)user_data;
	EProxyPrivate* priv = proxy->priv;
	GSList *ignore;

	priv->use_proxy = gconf_client_get_bool (client, KEY_GCONF_USE_HTTP_PROXY, NULL);
	if (priv->use_proxy == FALSE) {
		if (priv->uri) {
			soup_uri_free (priv->uri);
			priv->uri = NULL;
		}
		goto emit_signal;
	}
	
	proxy_server = gconf_client_get_string (client, KEY_GCONF_HTTP_PROXY_HOST, NULL);
	proxy_port = gconf_client_get_int (client, KEY_GCONF_HTTP_PROXY_PORT, NULL);
	uri = g_strdup_printf ("http://%s:%d", proxy_server, proxy_port);
	d(g_print ("ep_set_proxy: uri: %s\n", uri));

	if (regen_ign_host_list) {		
		if (priv->ign_hosts) {
			g_slist_foreach (priv->ign_hosts, (GFunc) g_free, NULL);
			g_slist_free (priv->ign_hosts);
			priv->ign_hosts = NULL;
		}
		
		if (priv->ign_addrs) {
			g_slist_foreach (priv->ign_addrs, (GFunc) g_free, NULL);
			g_slist_free (priv->ign_addrs);
			priv->ign_addrs = NULL;
		}

		ignore = gconf_client_get_list (client, KEY_GCONF_HTTP_PROXY_IGNORE_HOSTS, 
						GCONF_VALUE_STRING, NULL);
		if (ignore) {
			g_slist_foreach (ignore, (GFunc) ep_parse_ignore_host, proxy);
			g_slist_foreach (ignore, (GFunc) g_free, NULL);	
			g_slist_free (ignore);
		}
	} else if (set_auth) {
	
		use_auth = gconf_client_get_bool (client, KEY_GCONF_HTTP_USE_AUTH, NULL);
		if (use_auth == TRUE) {
			proxy_user = gconf_client_get_string (client, KEY_GCONF_HTTP_AUTH_USER, NULL);
			proxy_pw = gconf_client_get_string (client, KEY_GCONF_HTTP_AUTH_PW, NULL);
		
			if (uri)
				g_free (uri);

			uri = g_strdup_printf ("http://%s:%s@%s:%d", proxy_user, proxy_pw, proxy_server, proxy_port);
		} 
	}

	if (priv->uri) {
		soup_uri_free (priv->uri);
		priv->uri = NULL;
	}
	
	if (uri)
		priv->uri = soup_uri_new (uri);
	d(g_print ("system-proxy: uri: %s\n", uri));

 emit_signal:	
	g_signal_emit (proxy, signals[CHANGED], 0);

	g_free (uri);

	return;
}

static void
ep_setting_changed (GConfClient *client, guint32 cnxn_id,
		       GConfEntry *entry, gpointer user_data)
{
    const char *key;
    EProxy* proxy = (EProxy *)user_data;

    if (!proxy || !proxy->priv)
	    return;

    key = gconf_entry_get_key(entry);

    if (strcmp(key, KEY_GCONF_USE_HTTP_PROXY) == 0
        || strcmp(key, KEY_GCONF_HTTP_PROXY_IGNORE_HOSTS) == 0
        || strcmp(key, KEY_GCONF_HTTP_PROXY_HOST) == 0
        || strcmp(key, KEY_GCONF_HTTP_PROXY_PORT) == 0) {
	    gboolean regen_ign_host_list = FALSE;

	    if (strcmp(key, KEY_GCONF_HTTP_PROXY_IGNORE_HOSTS) == 0)
		    regen_ign_host_list = TRUE;
	    ep_set_proxy (client, user_data, regen_ign_host_list, FALSE);
	    d(g_print ("e-proxy.c:ep_settings_changed: proxy settings changed\n"));
    } else if (strcmp(key, KEY_GCONF_HTTP_AUTH_USER) == 0
               || strcmp(key, KEY_GCONF_HTTP_AUTH_PW) == 0
               || strcmp(key, KEY_GCONF_HTTP_USE_AUTH) == 0) {
	    ep_set_proxy (client, user_data, FALSE, TRUE);
	    d(g_print ("e-proxy.c:ep_settings_changed: auth settings changed\n"));	    
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

	/* Listen to the changes in the evolution-shell path */
	gconf_client_add_dir (client, PATH_GCONF_NETWORK_CONFIG,
			      GCONF_CLIENT_PRELOAD_ONELEVEL, NULL);

	if (proxy->priv->notify_id == 0)
		proxy->priv->notify_id = gconf_client_notify_add (client, PATH_GCONF_NETWORK_CONFIG,
								  ep_setting_changed, (gpointer)proxy, 
								  NULL, NULL);
	
	ep_set_proxy (client, proxy, TRUE, TRUE);

	g_object_unref (client);
}

SoupURI*
e_proxy_peek_uri (EProxy* proxy)
{
	if (!proxy || !proxy->priv)
		return NULL;
	
	return proxy->priv->uri;
}

gboolean
e_proxy_require_proxy_for_uri (EProxy* proxy, const char* uri)
{
	SoupURI *srv_uri = NULL;
	gboolean ret = FALSE;

	if (!uri || !proxy || !proxy->priv)
		return ret;

	if (!proxy->priv->use_proxy) {
		d(g_print ("[%s] don't need a proxy to connect to internet\n", uri));
		return ret;
	}

	srv_uri = soup_uri_new (uri);
	
	ret = ep_need_proxy (proxy, srv_uri->host);

	soup_uri_free (srv_uri);

	return ret;
}
