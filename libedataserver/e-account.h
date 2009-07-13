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

#ifndef __E_ACCOUNT__
#define __E_ACCOUNT__

#include <glib-object.h>

G_BEGIN_DECLS

#define E_TYPE_ACCOUNT            (e_account_get_type ())
#define E_ACCOUNT(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), E_TYPE_ACCOUNT, EAccount))
#define E_ACCOUNT_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), E_TYPE_ACCOUNT, EAccountClass))
#define E_IS_ACCOUNT(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), E_TYPE_ACCOUNT))
#define E_IS_ACCOUNT_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((obj), E_TYPE_ACCOUNT))

typedef enum _e_account_item_t {
	E_ACCOUNT_NAME,

	E_ACCOUNT_ID_NAME,
	E_ACCOUNT_ID_ADDRESS,
	E_ACCOUNT_ID_REPLY_TO,
	E_ACCOUNT_ID_ORGANIZATION,
	E_ACCOUNT_ID_SIGNATURE,

	E_ACCOUNT_SOURCE_URL,	/* what about separating out host/user/path settings??  sigh */
	E_ACCOUNT_SOURCE_KEEP_ON_SERVER,
	E_ACCOUNT_SOURCE_AUTO_CHECK,
	E_ACCOUNT_SOURCE_AUTO_CHECK_TIME,
	E_ACCOUNT_SOURCE_SAVE_PASSWD,

	E_ACCOUNT_TRANSPORT_URL,
	E_ACCOUNT_TRANSPORT_SAVE_PASSWD,

	E_ACCOUNT_DRAFTS_FOLDER_URI,
	E_ACCOUNT_SENT_FOLDER_URI,

	E_ACCOUNT_CC_ALWAYS,
	E_ACCOUNT_CC_ADDRS,

	E_ACCOUNT_BCC_ALWAYS,
	E_ACCOUNT_BCC_ADDRS,

	E_ACCOUNT_RECEIPT_POLICY,

	E_ACCOUNT_PGP_KEY,
	E_ACCOUNT_PGP_ENCRYPT_TO_SELF,
	E_ACCOUNT_PGP_ALWAYS_SIGN,
	E_ACCOUNT_PGP_NO_IMIP_SIGN,
	E_ACCOUNT_PGP_ALWAYS_TRUST,

	E_ACCOUNT_SMIME_SIGN_KEY,
	E_ACCOUNT_SMIME_ENCRYPT_KEY,
	E_ACCOUNT_SMIME_SIGN_DEFAULT,
	E_ACCOUNT_SMIME_ENCRYPT_TO_SELF,
	E_ACCOUNT_SMIME_ENCRYPT_DEFAULT,

	E_ACCOUNT_PROXY_PARENT_UID,

	E_ACCOUNT_ITEM_LAST
} e_account_item_t;

typedef enum _e_account_access_t {
	E_ACCOUNT_ACCESS_WRITE = 1<<0
} e_account_access_t;

typedef struct _EAccountIdentity {
	gchar *name;
	gchar *address;
	gchar *reply_to;
	gchar *organization;

	gchar *sig_uid;
} EAccountIdentity;

typedef enum _EAccountReceiptPolicy {
	E_ACCOUNT_RECEIPT_NEVER,
	E_ACCOUNT_RECEIPT_ASK,
	E_ACCOUNT_RECEIPT_ALWAYS
} EAccountReceiptPolicy;

typedef struct _EAccountService {
	gchar *url;
	gboolean keep_on_server;
	gboolean auto_check;
	gint auto_check_time;
	gboolean save_passwd;
	gboolean get_password_canceled;
} EAccountService;

typedef struct _EAccount {
	GObject parent_object;

	gchar *name;
	gchar *uid;

	gboolean enabled;

	EAccountIdentity *id;
	EAccountService *source;
	EAccountService *transport;

	gchar *drafts_folder_uri, *sent_folder_uri, *templates_folder_uri;

	gboolean always_cc;
	gchar *cc_addrs;
	gboolean always_bcc;
	gchar *bcc_addrs;

	EAccountReceiptPolicy receipt_policy;

	gchar *pgp_key;
	gboolean pgp_encrypt_to_self;
	gboolean pgp_always_sign;
	gboolean pgp_no_imip_sign;
	gboolean pgp_always_trust;

	gchar *parent_uid;

	gchar *smime_sign_key;
	gchar *smime_encrypt_key;
	gboolean smime_sign_default;
	gboolean smime_encrypt_to_self;
	gboolean smime_encrypt_default;
} EAccount;

typedef struct {
	GObjectClass parent_class;

	void (*changed)(EAccount *, gint field);
} EAccountClass;

GType     e_account_get_type (void);

EAccount *e_account_new          (void);

EAccount *e_account_new_from_xml (const gchar *xml);

gboolean  e_account_set_from_xml (EAccount   *account,
				  const gchar *xml);

void      e_account_import       (EAccount   *dest,
				  EAccount   *src);

gchar     *e_account_to_xml       (EAccount   *account);

gchar     *e_account_uid_from_xml (const gchar *xml);

const gchar *e_account_get_string (EAccount *,
				  e_account_item_t type);

gint       e_account_get_int      (EAccount *,
				  e_account_item_t type);

gboolean  e_account_get_bool     (EAccount *,
				  e_account_item_t type);

void      e_account_set_string   (EAccount *,
				  e_account_item_t type, const gchar *);

void      e_account_set_int      (EAccount *,
				  e_account_item_t type, gint);

void      e_account_set_bool     (EAccount *,
				  e_account_item_t type, gboolean);

gboolean  e_account_writable     (EAccount *ea,
				  e_account_item_t type);

gboolean  e_account_writable_option (EAccount *ea,
				  const gchar *protocol,
				  const gchar *option);

G_END_DECLS

#endif /* __E_ACCOUNT__ */
