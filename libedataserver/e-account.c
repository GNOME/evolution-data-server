/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "e-account.h"

#include "e-uid.h"

#include <string.h>

#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xmlmemory.h>

#include <gconf/gconf-client.h>

#define d(x)

enum {
	CHANGED,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL];

static void finalize (GObject *);

G_DEFINE_TYPE (EAccount, e_account, G_TYPE_OBJECT)

/*
lock mail accounts	Relatively difficult -- involves redesign of the XML blobs which describe accounts
disable adding mail accounts	Simple -- can be done with just a Gconf key and some UI work to make assoc. widgets unavailable
disable editing mail accounts	Relatively difficult -- involves redesign of the XML blobs which describe accounts
disable removing mail accounts
lock default character encoding	Simple -- Gconf key + a little UI work to desensitize widgets, etc
disable free busy publishing
disable specific mime types (from being viewed)	90% done already (Unknown MIME types still pose a problem)
lock image loading preference
lock junk mail filtering settings
**  junk mail per account
lock work week
lock first day of work week
lock working hours
disable forward as icalendar
lock color options for tasks
lock default contact filing format
* forbid signatures	Simple -- can be done with just a Gconf key and some UI work to make assoc. widgets unavailable
* lock user to having 1 specific signature	Simple -- can be done with just a Gconf key and some UI work to make assoc. widgets unavailable
* forbid adding/removing signatures	Simple -- can be done with just a Gconf key and some UI work to make assoc. widgets unavailable
* lock each account to a certain signature	Relatively difficult -- involved redesign of the XML blobs which describe accounts
* set default folders
set trash emptying frequency
* lock displayed mail headers	Simple -- can be done with just a Gconf key and some UI work to make assoc. widgets unavailable
* lock authentication type (for incoming mail)	Relatively difficult -- involves redesign of the XML blobs which describe accounts
* lock authentication type (for outgoing mail)	Relatively difficult -- involves redesign of the XML blobs which describe accounts
* lock minimum check mail on server frequency	Simple -- can be done with just a Gconf key and some UI work to make assoc. widgets unavailable
** lock save password
* require ssl always	Relatively difficult -- involves redesign of the XML blobs which describe accounts
** lock imap subscribed folder option
** lock filtering of inbox
** lock source account/options
** lock destination account/options
*/

static void
e_account_class_init (EAccountClass *account_class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (account_class);

	/* virtual method override */
	object_class->finalize = finalize;

	signals[CHANGED] =
		g_signal_new("changed",
			     G_OBJECT_CLASS_TYPE (object_class),
			     G_SIGNAL_RUN_LAST,
			     G_STRUCT_OFFSET (EAccountClass, changed),
			     NULL, NULL,
			     g_cclosure_marshal_VOID__INT,
			     G_TYPE_NONE, 1,
			     G_TYPE_INT);
}

static void
e_account_init (EAccount *account)
{
	account->id = g_new0 (EAccountIdentity, 1);
	account->source = g_new0 (EAccountService, 1);
	account->transport = g_new0 (EAccountService, 1);

	account->parent_uid = NULL;

	account->source->auto_check = FALSE;
	account->source->auto_check_time = 10;
}

static void
identity_destroy (EAccountIdentity *id)
{
	if (!id)
		return;

	g_free (id->name);
	g_free (id->address);
	g_free (id->reply_to);
	g_free (id->organization);
	g_free (id->sig_uid);

	g_free (id);
}

static void
service_destroy (EAccountService *service)
{
	if (!service)
		return;

	g_free (service->url);

	g_free (service);
}

static void
finalize (GObject *object)
{
	EAccount *account = E_ACCOUNT (object);

	g_free (account->name);
	g_free (account->uid);

	identity_destroy (account->id);
	service_destroy (account->source);
	service_destroy (account->transport);

	g_free (account->drafts_folder_uri);
	g_free (account->sent_folder_uri);

	g_free (account->cc_addrs);
	g_free (account->bcc_addrs);

	g_free (account->pgp_key);
	g_free (account->smime_sign_key);
	g_free (account->smime_encrypt_key);

	g_free (account->parent_uid);

	G_OBJECT_CLASS (e_account_parent_class)->finalize (object);
}

/**
 * e_account_new:
 *
 * Returns: a blank new account which can be filled in and
 * added to an #EAccountList.
 **/
EAccount *
e_account_new (void)
{
	EAccount *account;

	account = g_object_new (E_TYPE_ACCOUNT, NULL);
	account->uid = e_uid_new ();

	return account;
}

/**
 * e_account_new_from_xml:
 * @xml: an XML account description
 *
 * Returns: a new #EAccount based on the data in @xml, or %NULL
 * if @xml could not be parsed as valid account data.
 **/
EAccount *
e_account_new_from_xml (const gchar *xml)
{
	EAccount *account;

	account = g_object_new (E_TYPE_ACCOUNT, NULL);
	if (!e_account_set_from_xml (account, xml)) {
		g_object_unref (account);
		return NULL;
	}

	return account;
}

static gboolean
xml_set_bool (xmlNodePtr node, const gchar *name, gboolean *val)
{
	gboolean bool;
	xmlChar *buf;

	if ((buf = xmlGetProp (node, (xmlChar*)name))) {
		bool = (!strcmp ((gchar *)buf, "true") || !strcmp ((gchar *)buf, "yes"));
		xmlFree (buf);

		if (bool != *val) {
			*val = bool;
			return TRUE;
		}
	}

	return FALSE;
}

static gboolean
xml_set_int (xmlNodePtr node, const gchar *name, gint *val)
{
	gint number;
	xmlChar *buf;

	if ((buf = xmlGetProp (node, (xmlChar*)name))) {
		number = strtol ((gchar *)buf, NULL, 10);
		xmlFree (buf);

		if (number != *val) {
			*val = number;
			return TRUE;
		}
	}

	return FALSE;
}

static gboolean
xml_set_prop (xmlNodePtr node, const gchar *name, gchar **val)
{
	xmlChar *buf;
	gint res;

	buf = xmlGetProp (node, (xmlChar*)name);
	if (buf == NULL) {
		res = (*val != NULL);
		if (res) {
			g_free(*val);
			*val = NULL;
		}
	} else {
		res = *val == NULL || strcmp(*val, (gchar *)buf) != 0;
		if (res) {
			g_free(*val);
			*val = g_strdup((gchar *)buf);
		}
		xmlFree(buf);
	}

	return res;
}

static EAccountReceiptPolicy
str_to_receipt_policy (const xmlChar *str)
{
	if (!strcmp ((gchar *)str, "ask"))
		return E_ACCOUNT_RECEIPT_ASK;
	if (!strcmp ((gchar *)str, "always"))
		return E_ACCOUNT_RECEIPT_ALWAYS;

	return E_ACCOUNT_RECEIPT_NEVER;
}

static xmlChar*
receipt_policy_to_str (EAccountReceiptPolicy val)
{
	const gchar *ret = NULL;

	switch (val) {
	case E_ACCOUNT_RECEIPT_NEVER:
		ret = "never";
		break;
	case E_ACCOUNT_RECEIPT_ASK:
		ret = "ask";
		break;
	case E_ACCOUNT_RECEIPT_ALWAYS:
		ret = "always";
		break;
	}

	return (xmlChar*)ret;
}

static gboolean
xml_set_receipt_policy (xmlNodePtr node, const gchar *name, EAccountReceiptPolicy *val)
{
	EAccountReceiptPolicy new_val;
	xmlChar *buf;

	if ((buf = xmlGetProp (node, (xmlChar*)name))) {
		new_val = str_to_receipt_policy (buf);
		xmlFree (buf);

		if (new_val != *val) {
			*val = new_val;
			return TRUE;
		}
	}

	return FALSE;
}

static gboolean
xml_set_content (xmlNodePtr node, gchar **val)
{
	xmlChar *buf;
	gint res;

	buf = xmlNodeGetContent(node);
	if (buf == NULL) {
		res = (*val != NULL);
		if (res) {
			g_free(*val);
			*val = NULL;
		}
	} else {
		res = *val == NULL || strcmp(*val, (gchar *)buf) != 0;
		if (res) {
			g_free(*val);
			*val = g_strdup((gchar *)buf);
		}
		xmlFree(buf);
	}

	return res;
}

static gboolean
xml_set_identity (xmlNodePtr node, EAccountIdentity *id)
{
	gboolean changed = FALSE;

	for (node = node->children; node; node = node->next) {
		if (!strcmp ((gchar *)node->name, "name"))
			changed |= xml_set_content (node, &id->name);
		else if (!strcmp ((gchar *)node->name, "addr-spec"))
			changed |= xml_set_content (node, &id->address);
		else if (!strcmp ((gchar *)node->name, "reply-to"))
			changed |= xml_set_content (node, &id->reply_to);
		else if (!strcmp ((gchar *)node->name, "organization"))
			changed |= xml_set_content (node, &id->organization);
		else if (!strcmp ((gchar *)node->name, "signature")) {
			changed |= xml_set_prop (node, "uid", &id->sig_uid);
			if (!id->sig_uid) {

				/* XXX Migrate is supposed to "handle this" */

				/* set a fake sig uid so the migrate code can handle this */
				gboolean autogen = FALSE;
				gint sig_id = 0;

				xml_set_bool (node, "auto", &autogen);
				xml_set_int (node, "default", &sig_id);

				if (autogen) {
					id->sig_uid = g_strdup ("::0");
					changed = TRUE;
				} else if (sig_id) {
					id->sig_uid = g_strdup_printf ("::%d", sig_id + 1);
					changed = TRUE;
				}
			}
		}
	}

	return changed;
}

static gboolean
xml_set_service (xmlNodePtr node, EAccountService *service)
{
	gboolean changed = FALSE;

	changed |= xml_set_bool (node, "save-passwd", &service->save_passwd);
	changed |= xml_set_bool (node, "keep-on-server", &service->keep_on_server);

	changed |= xml_set_bool (node, "auto-check", &service->auto_check);
	changed |= xml_set_int (node, "auto-check-timeout", &service->auto_check_time);
	if (service->auto_check && service->auto_check_time <= 0) {
		service->auto_check = FALSE;
		service->auto_check_time = 0;
	}

	for (node = node->children; node; node = node->next) {
		if (!strcmp ((gchar *)node->name, "url")) {
			changed |= xml_set_content (node, &service->url);
			break;
		}
	}

	return changed;
}

/**
 * e_account_set_from_xml:
 * @account: an #EAccount
 * @xml: an XML account description.
 *
 * Changes @account to match @xml.
 *
 * Returns: %TRUE if @account was changed, %FALSE if @account
 * already matched @xml or @xml could not be parsed
 **/
gboolean
e_account_set_from_xml (EAccount *account, const gchar *xml)
{
	xmlNodePtr node, cur;
	xmlDocPtr doc;
	gboolean changed = FALSE;

	if (!(doc = xmlParseDoc ((xmlChar*)xml)))
		return FALSE;

	node = doc->children;
	if (strcmp ((gchar *)node->name, "account") != 0) {
		xmlFreeDoc (doc);
		return FALSE;
	}

	if (!account->uid)
		xml_set_prop (node, "uid", &account->uid);

	changed |= xml_set_prop (node, "name", &account->name);
	changed |= xml_set_bool (node, "enabled", &account->enabled);

	for (node = node->children; node; node = node->next) {
		if (!strcmp ((gchar *)node->name, "identity")) {
			changed |= xml_set_identity (node, account->id);
		} else if (!strcmp ((gchar *)node->name, "source")) {
			changed |= xml_set_service (node, account->source);
		} else if (!strcmp ((gchar *)node->name, "transport")) {
			changed |= xml_set_service (node, account->transport);
		} else if (!strcmp ((gchar *)node->name, "drafts-folder")) {
			changed |= xml_set_content (node, &account->drafts_folder_uri);
		} else if (!strcmp ((gchar *)node->name, "sent-folder")) {
			changed |= xml_set_content (node, &account->sent_folder_uri);
		} else if (!strcmp ((gchar *)node->name, "auto-cc")) {
			changed |= xml_set_bool (node, "always", &account->always_cc);
			changed |= xml_set_content (node, &account->cc_addrs);
		} else if (!strcmp ((gchar *)node->name, "auto-bcc")) {
			changed |= xml_set_bool (node, "always", &account->always_bcc);
			changed |= xml_set_content (node, &account->bcc_addrs);
		} else if (!strcmp ((gchar *)node->name, "receipt-policy")) {
			changed |= xml_set_receipt_policy (node, "policy", &account->receipt_policy);
		} else if (!strcmp ((gchar *)node->name, "pgp")) {
			changed |= xml_set_bool (node, "encrypt-to-self", &account->pgp_encrypt_to_self);
			changed |= xml_set_bool (node, "always-trust", &account->pgp_always_trust);
			changed |= xml_set_bool (node, "always-sign", &account->pgp_always_sign);
			changed |= xml_set_bool (node, "no-imip-sign", &account->pgp_no_imip_sign);

			if (node->children) {
				for (cur = node->children; cur; cur = cur->next) {
					if (!strcmp ((gchar *)cur->name, "key-id")) {
						changed |= xml_set_content (cur, &account->pgp_key);
						break;
					}
				}
			}
		} else if (!strcmp ((gchar *)node->name, "smime")) {
			changed |= xml_set_bool (node, "sign-default", &account->smime_sign_default);
			changed |= xml_set_bool (node, "encrypt-to-self", &account->smime_encrypt_to_self);
			changed |= xml_set_bool (node, "encrypt-default", &account->smime_encrypt_default);

			if (node->children) {
				for (cur = node->children; cur; cur = cur->next) {
					if (!strcmp ((gchar *)cur->name, "sign-key-id")) {
						changed |= xml_set_content (cur, &account->smime_sign_key);
					} else if (!strcmp ((gchar *)cur->name, "encrypt-key-id")) {
						changed |= xml_set_content (cur, &account->smime_encrypt_key);
						break;
					}
				}
			}
		} else if (!strcmp ((gchar *)node->name, "proxy")) {
			if (node->children) {
				for (cur = node->children; cur; cur = cur->next) {
					if (!strcmp ((gchar *)cur->name, "parent-uid")) {
						changed |= xml_set_content (cur, &account->parent_uid);
						break;
					}
				}
			}
		}
	}

	xmlFreeDoc (doc);

	g_signal_emit(account, signals[CHANGED], 0, -1);

	return changed;
}

/**
 * e_account_import:
 * @dest: destination account object
 * @src: source account object
 *
 * Import the settings from @src to @dest.
 **/
void
e_account_import (EAccount *dest, EAccount *src)
{
	g_free (dest->name);
	dest->name = g_strdup (src->name);

	dest->enabled = src->enabled;

	g_free (dest->id->name);
	dest->id->name = g_strdup (src->id->name);
	g_free (dest->id->address);
	dest->id->address = g_strdup (src->id->address);
	g_free (dest->id->reply_to);
	dest->id->reply_to = g_strdup (src->id->reply_to);
	g_free (dest->id->organization);
	dest->id->organization = g_strdup (src->id->organization);
	dest->id->sig_uid = g_strdup (src->id->sig_uid);

	g_free (dest->source->url);
	dest->source->url = g_strdup (src->source->url);
	dest->source->keep_on_server = src->source->keep_on_server;
	dest->source->auto_check = src->source->auto_check;
	dest->source->auto_check_time = src->source->auto_check_time;
	dest->source->save_passwd = src->source->save_passwd;

	g_free (dest->transport->url);
	dest->transport->url = g_strdup (src->transport->url);
	dest->transport->save_passwd = src->transport->save_passwd;

	g_free (dest->drafts_folder_uri);
	dest->drafts_folder_uri = g_strdup (src->drafts_folder_uri);

	g_free (dest->sent_folder_uri);
	dest->sent_folder_uri = g_strdup (src->sent_folder_uri);

	dest->always_cc = src->always_cc;
	g_free (dest->cc_addrs);
	dest->cc_addrs = g_strdup (src->cc_addrs);

	dest->always_bcc = src->always_bcc;
	g_free (dest->bcc_addrs);
	dest->bcc_addrs = g_strdup (src->bcc_addrs);

	dest->receipt_policy = src->receipt_policy;

	g_free (dest->pgp_key);
	dest->pgp_key = g_strdup (src->pgp_key);
	dest->pgp_encrypt_to_self = src->pgp_encrypt_to_self;
	dest->pgp_always_sign = src->pgp_always_sign;
	dest->pgp_no_imip_sign = src->pgp_no_imip_sign;
	dest->pgp_always_trust = src->pgp_always_trust;

	dest->smime_sign_default = src->smime_sign_default;
	g_free (dest->smime_sign_key);
	dest->smime_sign_key = g_strdup (src->smime_sign_key);

	dest->smime_encrypt_default = src->smime_encrypt_default;
	dest->smime_encrypt_to_self = src->smime_encrypt_to_self;
	g_free (dest->smime_encrypt_key);
	dest->smime_encrypt_key = g_strdup (src->smime_encrypt_key);

	g_signal_emit(dest, signals[CHANGED], 0, -1);
}

/**
 * e_account_to_xml:
 * @account: an #EAccount
 *
 * Returns: an XML representation of @account, which the caller
 * must free.
 **/
gchar *
e_account_to_xml (EAccount *account)
{
	xmlNodePtr root, node, id, src, xport;
	gchar *tmp, buf[20];
	xmlChar *xmlbuf;
	xmlDocPtr doc;
	gint n;

	doc = xmlNewDoc ((xmlChar*)"1.0");

	root = xmlNewDocNode (doc, NULL, (xmlChar*)"account", NULL);
	xmlDocSetRootElement (doc, root);

	xmlSetProp (root, (xmlChar*)"name", (xmlChar*)account->name);
	xmlSetProp (root, (xmlChar*)"uid", (xmlChar*)account->uid);
	xmlSetProp (root, (xmlChar*)"enabled", (xmlChar*)(account->enabled ? "true" : "false"));

	id = xmlNewChild (root, NULL, (xmlChar*)"identity", NULL);
	if (account->id->name)
		xmlNewTextChild (id, NULL, (xmlChar*)"name", (xmlChar*)account->id->name);
	if (account->id->address)
		xmlNewTextChild (id, NULL, (xmlChar*)"addr-spec", (xmlChar*)account->id->address);
	if (account->id->reply_to)
		xmlNewTextChild (id, NULL, (xmlChar*)"reply-to", (xmlChar*)account->id->reply_to);
	if (account->id->organization)
		xmlNewTextChild (id, NULL, (xmlChar*)"organization", (xmlChar*)account->id->organization);

	node = xmlNewChild (id, NULL, (xmlChar*)"signature",NULL);
	xmlSetProp (node, (xmlChar*)"uid", (xmlChar*)account->id->sig_uid);

	src = xmlNewChild (root, NULL, (xmlChar*)"source", NULL);
	xmlSetProp (src, (xmlChar*)"save-passwd", (xmlChar*)(account->source->save_passwd ? "true" : "false"));
	xmlSetProp (src, (xmlChar*)"keep-on-server", (xmlChar*)(account->source->keep_on_server ? "true" : "false"));
	xmlSetProp (src, (xmlChar*)"auto-check", (xmlChar*)(account->source->auto_check ? "true" : "false"));
	sprintf (buf, "%d", account->source->auto_check_time);
	xmlSetProp (src, (xmlChar*)"auto-check-timeout", (xmlChar*)buf);
	if (account->source->url)
		xmlNewTextChild (src, NULL, (xmlChar*)"url", (xmlChar*)account->source->url);

	xport = xmlNewChild (root, NULL, (xmlChar*)"transport", NULL);
	xmlSetProp (xport, (xmlChar*)"save-passwd", (xmlChar*)(account->transport->save_passwd ? "true" : "false"));
	if (account->transport->url)
		xmlNewTextChild (xport, NULL, (xmlChar*)"url", (xmlChar*)account->transport->url);

	xmlNewTextChild (root, NULL, (xmlChar*)"drafts-folder", (xmlChar*)account->drafts_folder_uri);
	xmlNewTextChild (root, NULL, (xmlChar*)"sent-folder", (xmlChar*)account->sent_folder_uri);

	node = xmlNewChild (root, NULL, (xmlChar*)"auto-cc", NULL);
	xmlSetProp (node, (xmlChar*)"always", (xmlChar*)(account->always_cc ? "true" : "false"));
	if (account->cc_addrs)
		xmlNewTextChild (node, NULL, (xmlChar*)"recipients", (xmlChar*)account->cc_addrs);

	node = xmlNewChild (root, NULL, (xmlChar*)"auto-bcc", NULL);
	xmlSetProp (node, (xmlChar*)"always", (xmlChar*)(account->always_bcc ? "true" : "false"));
	if (account->bcc_addrs)
		xmlNewTextChild (node, NULL, (xmlChar*)"recipients", (xmlChar*)account->bcc_addrs);

	node = xmlNewChild (root, NULL, (xmlChar*)"receipt-policy", NULL);
	xmlSetProp (node, (xmlChar*)"policy", receipt_policy_to_str (account->receipt_policy));

	node = xmlNewChild (root, NULL, (xmlChar*)"pgp", NULL);
	xmlSetProp (node, (xmlChar*)"encrypt-to-self", (xmlChar*)(account->pgp_encrypt_to_self ? "true" : "false"));
	xmlSetProp (node, (xmlChar*)"always-trust", (xmlChar*)(account->pgp_always_trust ? "true" : "false"));
	xmlSetProp (node, (xmlChar*)"always-sign", (xmlChar*)(account->pgp_always_sign ? "true" : "false"));
	xmlSetProp (node, (xmlChar*)"no-imip-sign", (xmlChar*)(account->pgp_no_imip_sign ? "true" : "false"));
	if (account->pgp_key)
		xmlNewTextChild (node, NULL, (xmlChar*)"key-id", (xmlChar*)account->pgp_key);

	node = xmlNewChild (root, NULL, (xmlChar*)"smime", NULL);
	xmlSetProp (node, (xmlChar*)"sign-default", (xmlChar*)(account->smime_sign_default ? "true" : "false"));
	xmlSetProp (node, (xmlChar*)"encrypt-default", (xmlChar*)(account->smime_encrypt_default ? "true" : "false"));
	xmlSetProp (node, (xmlChar*)"encrypt-to-self", (xmlChar*)(account->smime_encrypt_to_self ? "true" : "false"));
	if (account->smime_sign_key)
		xmlNewTextChild (node, NULL, (xmlChar*)"sign-key-id", (xmlChar*)account->smime_sign_key);
	if (account->smime_encrypt_key)
		xmlNewTextChild (node, NULL, (xmlChar*)"encrypt-key-id", (xmlChar*)account->smime_encrypt_key);

	if (account->parent_uid) {
		node = xmlNewChild (root, NULL, (xmlChar*)"proxy", NULL);
		xmlNewTextChild (node, NULL, (xmlChar*)"parent-uid", (xmlChar*)account->parent_uid);
	}

	xmlDocDumpMemory (doc, &xmlbuf, &n);
	xmlFreeDoc (doc);

	/* remap to glib memory */
	tmp = g_malloc (n + 1);
	memcpy (tmp, xmlbuf, n);
	tmp[n] = '\0';
	xmlFree (xmlbuf);

	return tmp;
}

/**
 * e_account_uid_from_xml:
 * @xml: an XML account description
 *
 * Returns: the permanent UID of the account described by @xml
 * (or %NULL if @xml could not be parsed or did not contain a uid).
 * The caller must free this string.
 **/
gchar *
e_account_uid_from_xml (const gchar *xml)
{
	xmlNodePtr node;
	xmlDocPtr doc;
	gchar *uid = NULL;

	if (!(doc = xmlParseDoc ((xmlChar *)xml)))
		return NULL;

	node = doc->children;
	if (strcmp ((gchar *)node->name, "account") != 0) {
		xmlFreeDoc (doc);
		return NULL;
	}

	xml_set_prop (node, "uid", &uid);
	xmlFreeDoc (doc);

	return uid;
}

enum {
	EAP_IMAP_SUBSCRIBED = 0,
	EAP_IMAP_NAMESPACE,
	EAP_FILTER_INBOX,
	EAP_FILTER_JUNK,
	EAP_FORCE_SSL,
	EAP_LOCK_SIGNATURE,
	EAP_LOCK_AUTH,
	EAP_LOCK_AUTOCHECK,
	EAP_LOCK_DEFAULT_FOLDERS,
	EAP_LOCK_SAVE_PASSWD,
	EAP_LOCK_SOURCE,
	EAP_LOCK_TRANSPORT
};

static struct _system_info {
	const gchar *key;
	guint32 perm;
} system_perms[] = {
	{ "imap_subscribed", 1<<EAP_IMAP_SUBSCRIBED },
	{ "imap_namespace", 1<<EAP_IMAP_NAMESPACE },
	{ "filter_inbox", 1<<EAP_FILTER_INBOX },
	{ "filter_junk", 1<<EAP_FILTER_JUNK },
	{ "ssl", 1<<EAP_FORCE_SSL },
	{ "signature", 1<<EAP_LOCK_SIGNATURE },
	{ "authtype", 1<<EAP_LOCK_AUTH },
	{ "autocheck", 1<<EAP_LOCK_AUTOCHECK },
	{ "default_folders", 1<<EAP_LOCK_DEFAULT_FOLDERS },
	{ "save_passwd" , 1<<EAP_LOCK_SAVE_PASSWD },
	{ "source", 1<<EAP_LOCK_SOURCE },
	{ "transport", 1<<EAP_LOCK_TRANSPORT },
};

#define TYPE_STRING (1)
#define TYPE_INT (2)
#define TYPE_BOOL (3)
#define TYPE_MASK (0xff)
#define TYPE_STRUCT (1<<8)

static struct _account_info {
	guint32 perms;
	guint32 type;
	guint offset;
	guint struct_offset;
} account_info[E_ACCOUNT_ITEM_LAST] = {
	{ /* E_ACCOUNT_NAME */ 0, TYPE_STRING, G_STRUCT_OFFSET(EAccount, name) },

	{ /* E_ACCOUNT_ID_NAME, */ 0, TYPE_STRING|TYPE_STRUCT, G_STRUCT_OFFSET(EAccount, id), G_STRUCT_OFFSET(EAccountIdentity, name) },
	{ /* E_ACCOUNT_ID_ADDRESS, */ 0, TYPE_STRING|TYPE_STRUCT, G_STRUCT_OFFSET(EAccount, id), G_STRUCT_OFFSET(EAccountIdentity, address) },
	{ /* E_ACCOUNT_ID_REPLY_TO, */ 0, TYPE_STRING|TYPE_STRUCT, G_STRUCT_OFFSET(EAccount, id), G_STRUCT_OFFSET(EAccountIdentity, reply_to) },
	{ /* E_ACCOUNT_ID_ORGANIZATION */ 0, TYPE_STRING|TYPE_STRUCT, G_STRUCT_OFFSET(EAccount, id), G_STRUCT_OFFSET(EAccountIdentity, organization) },
	{ /* E_ACCOUNT_ID_SIGNATURE */ 1<<EAP_LOCK_SIGNATURE, TYPE_STRING|TYPE_STRUCT, G_STRUCT_OFFSET(EAccount, id), G_STRUCT_OFFSET(EAccountIdentity, sig_uid) },

	{ /* E_ACCOUNT_SOURCE_URL */ 1<<EAP_LOCK_SOURCE, TYPE_STRING|TYPE_STRUCT, G_STRUCT_OFFSET(EAccount, source), G_STRUCT_OFFSET(EAccountService, url) },
	{ /* E_ACCOUNT_SOURCE_KEEP_ON_SERVER */ 0, TYPE_BOOL|TYPE_STRUCT, G_STRUCT_OFFSET(EAccount, source), G_STRUCT_OFFSET(EAccountService, keep_on_server) },
	{ /* E_ACCOUNT_SOURCE_AUTO_CHECK */ 1<<EAP_LOCK_AUTOCHECK, TYPE_BOOL|TYPE_STRUCT, G_STRUCT_OFFSET(EAccount, source), G_STRUCT_OFFSET(EAccountService, auto_check) },
	{ /* E_ACCOUNT_SOURCE_AUTO_CHECK_TIME */ 1<<EAP_LOCK_AUTOCHECK, TYPE_INT|TYPE_STRUCT, G_STRUCT_OFFSET(EAccount, source), G_STRUCT_OFFSET(EAccountService, auto_check_time) },
	{ /* E_ACCOUNT_SOURCE_SAVE_PASSWD */ 1<<EAP_LOCK_SAVE_PASSWD, TYPE_BOOL|TYPE_STRUCT, G_STRUCT_OFFSET(EAccount, source), G_STRUCT_OFFSET(EAccountService, save_passwd) },

	{ /* E_ACCOUNT_TRANSPORT_URL */ 1<<EAP_LOCK_TRANSPORT, TYPE_STRING|TYPE_STRUCT, G_STRUCT_OFFSET(EAccount, transport), G_STRUCT_OFFSET(EAccountService, url) },
	{ /* E_ACCOUNT_TRANSPORT_SAVE_PASSWD */ 1<<EAP_LOCK_SAVE_PASSWD, TYPE_BOOL|TYPE_STRUCT, G_STRUCT_OFFSET(EAccount, transport), G_STRUCT_OFFSET(EAccountService, save_passwd) },

	{ /* E_ACCOUNT_DRAFTS_FOLDER_URI */ 1<<EAP_LOCK_DEFAULT_FOLDERS, TYPE_STRING, G_STRUCT_OFFSET(EAccount, drafts_folder_uri) },
	{ /* E_ACCOUNT_SENT_FOLDER_URI */ 1<<EAP_LOCK_DEFAULT_FOLDERS, TYPE_STRING, G_STRUCT_OFFSET(EAccount, sent_folder_uri) },

	{ /* E_ACCOUNT_CC_ALWAYS */ 0, TYPE_BOOL, G_STRUCT_OFFSET(EAccount, always_cc) },
	{ /* E_ACCOUNT_CC_ADDRS */ 0, TYPE_STRING, G_STRUCT_OFFSET(EAccount, cc_addrs) },

	{ /* E_ACCOUNT_BCC_ALWAYS */ 0, TYPE_BOOL, G_STRUCT_OFFSET(EAccount, always_bcc) },
	{ /* E_ACCOUNT_BCC_ADDRS */ 0, TYPE_STRING, G_STRUCT_OFFSET(EAccount, bcc_addrs) },

	{ /* E_ACCOUNT_RECEIPT_POLICY */ 0, TYPE_INT, G_STRUCT_OFFSET(EAccount, receipt_policy) },

	{ /* E_ACCOUNT_PGP_KEY */ 0, TYPE_STRING, G_STRUCT_OFFSET(EAccount, pgp_key) },
	{ /* E_ACCOUNT_PGP_ENCRYPT_TO_SELF */ 0, TYPE_BOOL, G_STRUCT_OFFSET(EAccount, pgp_encrypt_to_self) },
	{ /* E_ACCOUNT_PGP_ALWAYS_SIGN */ 0, TYPE_BOOL, G_STRUCT_OFFSET(EAccount, pgp_always_sign) },
	{ /* E_ACCOUNT_PGP_NO_IMIP_SIGN */ 0, TYPE_BOOL, G_STRUCT_OFFSET(EAccount, pgp_no_imip_sign) },
	{ /* E_ACCOUNT_PGP_ALWAYS_TRUST */ 0, TYPE_BOOL, G_STRUCT_OFFSET(EAccount, pgp_always_trust) },

	{ /* E_ACCOUNT_SMIME_SIGN_KEY */ 0, TYPE_STRING, G_STRUCT_OFFSET(EAccount, smime_sign_key) },
	{ /* E_ACCOUNT_SMIME_ENCRYPT_KEY */ 0, TYPE_STRING, G_STRUCT_OFFSET(EAccount, smime_encrypt_key) },
	{ /* E_ACCOUNT_SMIME_SIGN_DEFAULT */ 0, TYPE_BOOL, G_STRUCT_OFFSET(EAccount, smime_sign_default) },
	{ /* E_ACCOUNT_SMIME_ENCRYPT_TO_SELF */ 0, TYPE_BOOL, G_STRUCT_OFFSET(EAccount, smime_encrypt_to_self) },
	{ /* E_ACCOUNT_SMIME_ENCRYPT_DEFAULT */ 0, TYPE_BOOL, G_STRUCT_OFFSET(EAccount, smime_encrypt_default) },

	{ /* E_ACCOUNT_PROXY_PARENT_UID, */ 0, TYPE_STRING, G_STRUCT_OFFSET(EAccount, parent_uid) },
};

static GHashTable *ea_option_table;
static GHashTable *ea_system_table;
static guint32 ea_perms;

static struct _option_info {
	const gchar *key;
	guint32 perms;
} ea_option_list[] = {
	{ "imap_use_lsub", 1<<EAP_IMAP_SUBSCRIBED },
	{ "imap_override_namespace", 1<<EAP_IMAP_NAMESPACE },
	{ "imap_filter", 1<<EAP_FILTER_INBOX },
	{ "imap_filter_junk", 1<<EAP_FILTER_JUNK },
	{ "imap_filter_junk_inbox", 1<<EAP_FILTER_JUNK },
	{ "*_use_ssl", 1<<EAP_FORCE_SSL },
	{ "*_auth", 1<<EAP_LOCK_AUTH },
};

#define LOCK_BASE "/apps/evolution/lock/mail/accounts"

static void
ea_setting_notify (GConfClient *gconf, guint cnxn_id, GConfEntry *entry, gpointer crap)
{
	GConfValue *value;
	gchar *tkey;
	struct _system_info *info;

	g_return_if_fail (gconf_entry_get_key (entry) != NULL);

	if (!(value = gconf_entry_get_value (entry)))
		return;

	tkey = strrchr(entry->key, '/');
	g_return_if_fail (tkey != NULL);

	info = g_hash_table_lookup(ea_system_table, tkey+1);
	if (info) {
		if (gconf_value_get_bool(value))
			ea_perms |= info->perm;
		else
			ea_perms &= ~info->perm;
	}
}

static void
ea_setting_setup (void)
{
	GConfClient *gconf = gconf_client_get_default();
	GConfEntry *entry;
	GError *err = NULL;
	gint i;
	gchar key[64];

	if (ea_option_table != NULL)
		return;

	ea_option_table = g_hash_table_new(g_str_hash, g_str_equal);
	for (i = 0; i < G_N_ELEMENTS (ea_option_list); i++)
		g_hash_table_insert(ea_option_table, (gpointer) ea_option_list[i].key, &ea_option_list[i]);

	gconf_client_add_dir(gconf, LOCK_BASE, GCONF_CLIENT_PRELOAD_NONE, NULL);

	ea_system_table = g_hash_table_new(g_str_hash, g_str_equal);
	for (i = 0; i < G_N_ELEMENTS (system_perms); i++) {
		g_hash_table_insert(ea_system_table, (gchar *)system_perms[i].key, &system_perms[i]);
		sprintf(key, LOCK_BASE "/%s", system_perms[i].key);
		entry = gconf_client_get_entry(gconf, key, NULL, TRUE, &err);
		if (entry) {
			ea_setting_notify(gconf, 0, entry, NULL);
			gconf_entry_free(entry);
		}
	}

	if (err) {
		g_warning("Could not load account lock settings: %s", err->message);
		g_error_free(err);
	}

	gconf_client_notify_add(gconf, LOCK_BASE, (GConfClientNotifyFunc)ea_setting_notify, NULL, NULL, NULL);
	g_object_unref(gconf);
}

/* look up the item in the structure or the substructure using our table of reflection data */
#define addr(ea, type) \
	((account_info[type].type & TYPE_STRUCT)? \
	(((gchar **)(((gchar *)ea)+account_info[type].offset))[0] + account_info[type].struct_offset): \
	(((gchar *)ea)+account_info[type].offset))

const gchar *
e_account_get_string (EAccount *ea, e_account_item_t type)
{
	g_return_val_if_fail (ea != NULL, NULL);
	return *((const gchar **)addr(ea, type));
}

gint
e_account_get_int (EAccount *ea, e_account_item_t type)
{
	g_return_val_if_fail (ea != NULL, 0);
	return *((gint *)addr(ea, type));
}

gboolean
e_account_get_bool (EAccount *ea, e_account_item_t type)
{
	g_return_val_if_fail (ea != NULL, FALSE);
	return *((gboolean *)addr(ea, type));
}

#if d(!)0
static void
dump_account (EAccount *ea)
{
	gchar *xml;

	printf("Account changed\n");
	xml = e_account_to_xml(ea);
	printf(" ->\n%s\n", xml);
	g_free(xml);
}
#endif

/* TODO: should it return true if it changed? */
void
e_account_set_string (EAccount *ea, e_account_item_t type, const gchar *val)
{
	gchar **p;

	g_return_if_fail (ea != NULL);

	if (!e_account_writable(ea, type)) {
		g_warning("Trying to set non-writable option account value");
	} else {
		p = (gchar **)addr(ea, type);
		d(printf("Setting string %d: old '%s' new '%s'\n", type, *p, val));
		if (*p != val
		    && (*p == NULL || val == NULL || strcmp(*p, val) != 0)) {
			g_free(*p);
			*p = g_strdup(val);
			d(dump_account(ea));
			g_signal_emit(ea, signals[CHANGED], 0, type);
		}
	}
}

void
e_account_set_int (EAccount *ea, e_account_item_t type, gint val)
{
	g_return_if_fail (ea != NULL);

	if (!e_account_writable(ea, type)) {
		g_warning("Trying to set non-writable option account value");
	} else {
		gint *p = (gint *)addr(ea, type);

		if (*p != val) {
			*p = val;
			d(dump_account(ea));
			g_signal_emit(ea, signals[CHANGED], 0, type);
		}
	}
}

void
e_account_set_bool (EAccount *ea, e_account_item_t type, gboolean val)
{
	g_return_if_fail (ea != NULL);

	if (!e_account_writable(ea, type)) {
		g_warning("Trying to set non-writable option account value");
	} else {
		gboolean *p = (gboolean *)addr(ea, type);

		if (*p != val) {
			*p = val;
			d(dump_account(ea));
			g_signal_emit(ea, signals[CHANGED], 0, type);
		}
	}
}

gboolean
e_account_writable_option (EAccount *ea, const gchar *protocol, const gchar *option)
{
	gchar *key;
	struct _option_info *info;

	ea_setting_setup();

	key = alloca(strlen(protocol)+strlen(option)+2);
	sprintf(key, "%s_%s", protocol, option);

	info = g_hash_table_lookup(ea_option_table, key);
	if (info == NULL) {
		sprintf(key, "*_%s", option);
		info = g_hash_table_lookup(ea_option_table, key);
	}

	d(printf("checking writable option '%s' perms=%08x\n", option, info?info->perms:0));

	return info == NULL
		|| (info->perms & ea_perms) == 0;
}

gboolean
e_account_writable (EAccount *ea, e_account_item_t type)
{
	ea_setting_setup();

	return (account_info[type].perms & ea_perms) == 0;
}
