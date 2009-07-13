/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-provider.h :  provider definition  */

/*
 *
 * Authors:
 *  Bertrand Guiheneuf <bertrand@helixcode.com>
 *  Jeffrey Stedfast <fejj@ximian.com>
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301
 * USA
 */

#ifndef CAMEL_PROVIDER_H
#define CAMEL_PROVIDER_H 1

#include <camel/camel-types.h>
#include <camel/camel-object.h>
#include <camel/camel-exception.h>
#include <camel/camel-url.h>

#define CAMEL_PROVIDER(obj) ((CamelProvider *)(obj))

G_BEGIN_DECLS

typedef enum {
	CAMEL_PROVIDER_STORE,
	CAMEL_PROVIDER_TRANSPORT,
	CAMEL_NUM_PROVIDER_TYPES
} CamelProviderType;

extern gchar *camel_provider_type_name[CAMEL_NUM_PROVIDER_TYPES];

/* Provider flags:
 *
 * _IS_REMOTE   the provider works with remote data
 * _IS_LOCAL    it can be used as a backend for local folder
 *                tree folders. (*Not* just the opposite of _IS_REMOTE)
 * _IS_SOURCE   mail arrives there, so it should be offered as an
 *                option in the mail config dialog.
 * _IS_STORAGE  mail is stored there. it will appear in the folder tree.
 * _IS_EXTERNAL it appears in the folder tree but is not created by
 *                the mail component.
 * _HAS_LICENSE  the provider configuration first needs the license to
 *		   be accepted.
 */
#define CAMEL_PROVIDER_IS_REMOTE	(1 << 0)
#define CAMEL_PROVIDER_IS_LOCAL		(1 << 1)
#define CAMEL_PROVIDER_IS_EXTERNAL	(1 << 2)
#define CAMEL_PROVIDER_IS_SOURCE	(1 << 3)
#define CAMEL_PROVIDER_IS_STORAGE	(1 << 4)
#define CAMEL_PROVIDER_SUPPORTS_SSL	(1 << 5)
#define CAMEL_PROVIDER_HAS_LICENSE      (1 << 6)
#define CAMEL_PROVIDER_DISABLE_SENT_FOLDER (1 << 7)

/* Flags for url_flags. "ALLOW" means the config dialog will let the
 * user configure it. "NEED" implies "ALLOW" but means the user must
 * configure it. Service code can assume that any url part for which
 * it has set the NEED flag will be set when the service is
 * created. "HIDE" also implies "ALLOW", but the setting will be
 * hidden/no widgets created for it.
 */
#define CAMEL_URL_PART_USER	 (1 << 0)
#define CAMEL_URL_PART_AUTH	 (1 << 1)
#define CAMEL_URL_PART_PASSWORD	 (1 << 2)
#define CAMEL_URL_PART_HOST	 (1 << 3)
#define CAMEL_URL_PART_PORT	 (1 << 4)
#define CAMEL_URL_PART_PATH	 (1 << 5)
#define CAMEL_URL_PART_PATH_DIR  (1 << 6)

#define CAMEL_URL_PART_NEED	       8
#define CAMEL_URL_PART_HIDDEN	(CAMEL_URL_PART_NEED + 8)

/* Use these macros to test a provider's url_flags */
#define CAMEL_PROVIDER_ALLOWS(prov, flags) (prov->url_flags & (flags | (flags << CAMEL_URL_PART_NEED) | (flags << CAMEL_URL_PART_HIDDEN)))
#define CAMEL_PROVIDER_NEEDS(prov, flags) (prov->url_flags & (flags << CAMEL_URL_PART_NEED))
#define CAMEL_PROVIDER_HIDDEN(prov, flags) (prov->url_flags & (flags << CAMEL_URL_PART_HIDDEN))

/* Providers use these macros to actually define their url_flags */
#define CAMEL_URL_ALLOW_USER	 (CAMEL_URL_PART_USER)
#define CAMEL_URL_ALLOW_AUTH	 (CAMEL_URL_PART_AUTH)
#define CAMEL_URL_ALLOW_PASSWORD (CAMEL_URL_PART_PASSWORD)
#define CAMEL_URL_ALLOW_HOST	 (CAMEL_URL_PART_HOST)
#define CAMEL_URL_ALLOW_PORT	 (CAMEL_URL_PART_PORT)
#define CAMEL_URL_ALLOW_PATH	 (CAMEL_URL_PART_PATH)

#define CAMEL_URL_NEED_USER	 (CAMEL_URL_PART_USER << CAMEL_URL_PART_NEED)
#define CAMEL_URL_NEED_AUTH	 (CAMEL_URL_PART_AUTH << CAMEL_URL_PART_NEED)
#define CAMEL_URL_NEED_PASSWORD	 (CAMEL_URL_PART_PASSWORD << CAMEL_URL_PART_NEED)
#define CAMEL_URL_NEED_HOST	 (CAMEL_URL_PART_HOST << CAMEL_URL_PART_NEED)
#define CAMEL_URL_NEED_PORT	 (CAMEL_URL_PART_PORT << CAMEL_URL_PART_NEED)
#define CAMEL_URL_NEED_PATH	 (CAMEL_URL_PART_PATH << CAMEL_URL_PART_NEED)
#define CAMEL_URL_NEED_PATH_DIR  (CAMEL_URL_PART_PATH_DIR << CAMEL_URL_PART_NEED)

#define CAMEL_URL_HIDDEN_USER	 (CAMEL_URL_PART_USER << CAMEL_URL_PART_HIDDEN)
#define CAMEL_URL_HIDDEN_AUTH	 (CAMEL_URL_PART_AUTH << CAMEL_URL_PART_HIDDEN)
#define CAMEL_URL_HIDDEN_PASSWORD	 (CAMEL_URL_PART_PASSWORD << CAMEL_URL_PART_HIDDEN)
#define CAMEL_URL_HIDDEN_HOST	 (CAMEL_URL_PART_HOST << CAMEL_URL_PART_HIDDEN)
#define CAMEL_URL_HIDDEN_PORT	 (CAMEL_URL_PART_PORT << CAMEL_URL_PART_HIDDEN)
#define CAMEL_URL_HIDDEN_PATH	 (CAMEL_URL_PART_PATH << CAMEL_URL_PART_HIDDEN)

#define CAMEL_URL_FRAGMENT_IS_PATH  (1 << 30) /* url uses fragment for folder name path, not path */
#define CAMEL_URL_PATH_IS_ABSOLUTE (1 << 31)

#define CAMEL_PROVIDER_IS_STORE_AND_TRANSPORT(prov) (prov->object_types[CAMEL_PROVIDER_STORE] && prov->object_types[CAMEL_PROVIDER_TRANSPORT])

/* Generic extra config stuff */
typedef enum {
	CAMEL_PROVIDER_CONF_END,
	CAMEL_PROVIDER_CONF_SECTION_START,
	CAMEL_PROVIDER_CONF_SECTION_END,
	CAMEL_PROVIDER_CONF_CHECKBOX,
	CAMEL_PROVIDER_CONF_CHECKSPIN,
	CAMEL_PROVIDER_CONF_ENTRY,
	CAMEL_PROVIDER_CONF_LABEL,
	CAMEL_PROVIDER_CONF_HIDDEN,
	CAMEL_PROVIDER_CONF_OPTIONS
} CamelProviderConfType;

typedef struct {
	CamelProviderConfType type;
	const gchar *name, *depname;
	const gchar *text, *value;
} CamelProviderConfEntry;

/* Some defaults */
#define CAMEL_PROVIDER_CONF_DEFAULT_USERNAME  { CAMEL_PROVIDER_CONF_LABEL, "username", NULL, N_("User_name:"), NULL }
#define CAMEL_PROVIDER_CONF_DEFAULT_HOSTNAME  { CAMEL_PROVIDER_CONF_LABEL, "hostname", NULL, N_("_Host:"), NULL }
#define CAMEL_PROVIDER_CONF_DEFAULT_PATH      { CAMEL_PROVIDER_CONF_ENTRY, "path", NULL, N_("_Path:"), "" }

typedef gint (*CamelProviderAutoDetectFunc) (CamelURL *url, GHashTable **auto_detected, CamelException *ex);

typedef struct {
	/* Provider name used in CamelURLs. */
	const gchar *protocol;

	/* Provider name as used by people. (May be the same as protocol) */
	const gchar *name;

	/* Description of the provider. A novice user should be able
	 * to read this description, and the information provided by
	 * an ISP, IS department, etc, and determine whether or not
	 * this provider is relevant to him, and if so, which
	 * information goes with it.
	 */
	const gchar *description;

	/* The category of message that this provider works with.
	 * (evolution-mail will only list a provider in the store/transport
	 * config dialogs if its domain is "mail".)
	 */
	const gchar *domain;

	/* Flags describing the provider, flags describing its URLs */
	gint flags, url_flags;

	/* The ConfEntry and AutoDetect functions will probably be
	 * DEPRECATED in a future release */

	/* Extra configuration information */
	CamelProviderConfEntry *extra_conf;

	/* auto-detection function */
	CamelProviderAutoDetectFunc auto_detect;

	/* CamelType(s) of its store and/or transport. If both are
	 * set, then they are assumed to be linked together and the
	 * transport type can only be used in an account that also
	 * uses the store type (eg, Exchange or NNTP).
	 */
	CamelType object_types[CAMEL_NUM_PROVIDER_TYPES];

	/* GList of CamelServiceAuthTypes the provider supports */
	GList *authtypes;

	CamelObjectBag *service_cache[CAMEL_NUM_PROVIDER_TYPES];

	GHashFunc url_hash;
	GCompareFunc url_equal;

	/* gettext translation domain (NULL for providers in the
	 * evolution source tree).
	 */
	const gchar *translation_domain;

	/* This string points to the provider's gconf key value
	 */
	const gchar *license;

	/* This holds the license file name [ ascii text format ] containing
	 * the license agreement. This should be the absolute file path. This
	 * is read only when the HAS_LICENSE flag is set
	 */
	const gchar *license_file;

	/* Private to the provider */
	gpointer priv;
} CamelProvider;

typedef struct _CamelProviderModule CamelProviderModule;

struct _CamelProviderModule {
	gchar *path;
	GSList *types;
	guint loaded:1;
};

void camel_provider_init(void);

void camel_provider_load(const gchar *path, CamelException *ex);
void camel_provider_register(CamelProvider *provider);
GList *camel_provider_list(gboolean load);
CamelProvider *camel_provider_get(const gchar *url_string, CamelException *ex);

/* This is defined by each module, not by camel-provider.c. */
void camel_provider_module_init(void);

gint camel_provider_auto_detect (CamelProvider *provider, CamelURL *url,
				GHashTable **auto_detected, CamelException *ex);

G_END_DECLS

#endif /* CAMEL_PROVIDER_H */
