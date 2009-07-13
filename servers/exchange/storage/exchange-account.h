/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* Copyright (C) 2001-2004 Novell, Inc. */

#ifndef __EXCHANGE_ACCOUNT_H__
#define __EXCHANGE_ACCOUNT_H__

#include <exchange-types.h>
#include <exchange-constants.h>
#include "e2k-autoconfig.h"
#include "e2k-context.h"
#include "e2k-global-catalog.h"
#include "e2k-security-descriptor.h"
#include "e-folder.h"
#include <libedataserver/e-account-list.h>
#include <gtk/gtk.h>

G_BEGIN_DECLS

#define EXCHANGE_TYPE_ACCOUNT            (exchange_account_get_type ())
#define EXCHANGE_ACCOUNT(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), EXCHANGE_TYPE_ACCOUNT, ExchangeAccount))
#define EXCHANGE_ACCOUNT_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), EXCHANGE_TYPE_ACCOUNT, ExchangeAccountClass))
#define EXCHANGE_IS_ACCOUNT(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), EXCHANGE_TYPE_ACCOUNT))
#define EXCHANGE_IS_ACCOUNT_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((obj), EXCHANGE_TYPE_ACCOUNT))

struct _ExchangeAccount {
	GObject parent;

	ExchangeAccountPrivate *priv;

	/* account_name is the user-specified UTF8 display name.
	 * account_filename is "username@hostname" run through
	 * e_filename_make_safe.
	 */
	gchar *account_name, *account_filename, *storage_dir;
	gchar *exchange_server, *home_uri, *public_uri;
	gchar *legacy_exchange_dn, *default_timezone;

	gboolean filter_inbox, filter_junk, filter_junk_inbox_only;
	gdouble mbox_size;
};

struct _ExchangeAccountClass {
	GObjectClass parent_class;

	/* signals */
	void (*connected) (ExchangeAccount *, E2kContext *);

	void (*new_folder) (ExchangeAccount *, EFolder *);
	void (*removed_folder) (ExchangeAccount *, EFolder *);
};

typedef enum {
	EXCHANGE_ACCOUNT_CONFIG_ERROR,
	EXCHANGE_ACCOUNT_PASSWORD_WEAK_ERROR,
	EXCHANGE_ACCOUNT_PASSWORD_CHANGE_FAILED,
	EXCHANGE_ACCOUNT_PASSWORD_CHANGE_SUCCESS,
	EXCHANGE_ACCOUNT_OFFLINE,
	EXCHANGE_ACCOUNT_PASSWORD_INCORRECT,
	EXCHANGE_ACCOUNT_DOMAIN_ERROR,
	EXCHANGE_ACCOUNT_MAILBOX_NA,
	EXCHANGE_ACCOUNT_VERSION_ERROR,
	EXCHANGE_ACCOUNT_WSS_ERROR,
	EXCHANGE_ACCOUNT_NO_MAILBOX,
	EXCHANGE_ACCOUNT_RESOLVE_ERROR,
	EXCHANGE_ACCOUNT_CONNECT_ERROR,
	EXCHANGE_ACCOUNT_PASSWORD_EXPIRED,
	EXCHANGE_ACCOUNT_UNKNOWN_ERROR,
	EXCHANGE_ACCOUNT_QUOTA_RECIEVE_ERROR,
	EXCHANGE_ACCOUNT_QUOTA_SEND_ERROR,
	EXCHANGE_ACCOUNT_QUOTA_WARN,
	EXCHANGE_ACCOUNT_CONNECT_SUCCESS
} ExchangeAccountResult;

GType                  exchange_account_get_type             (void);
ExchangeAccount       *exchange_account_new                  (EAccountList *account_list,
							      EAccount     *adata);
E2kContext            *exchange_account_get_context          (ExchangeAccount  *acct);
E2kGlobalCatalog      *exchange_account_get_global_catalog   (ExchangeAccount  *acct);

EAccount	      *exchange_account_fetch		     (ExchangeAccount *acct);
gchar                  *exchange_account_get_account_uri_param (ExchangeAccount *acct, const gchar *param);

const gchar            *exchange_account_get_standard_uri     (ExchangeAccount  *acct,
							      const gchar       *item);

gchar                  *exchange_account_get_standard_uri_for (ExchangeAccount  *acct,
							      const gchar       *home_uri,
							      const gchar       *std_uri_prop);
gchar                  *exchange_account_get_foreign_uri      (ExchangeAccount  *acct,
							      E2kGlobalCatalogEntry *entry,
							      const gchar       *std_uri_prop);
ExchangeHierarchy     *exchange_account_get_hierarchy_by_email (ExchangeAccount *account, const gchar *email);

gchar		      *exchange_account_get_authtype	     (ExchangeAccount *account);

E2kContext            *exchange_account_connect              (ExchangeAccount  *acct,
							      const gchar *pword,
							      ExchangeAccountResult *result);

EFolder               *exchange_account_get_folder           (ExchangeAccount  *acct,
							      const gchar       *path_or_uri);
GPtrArray             *exchange_account_get_folders          (ExchangeAccount  *acct);

GPtrArray	       *exchange_account_get_folder_tree      (ExchangeAccount *account, gchar * path);

ExchangeHierarchy     *exchange_account_get_hierarchy_by_type	      (ExchangeAccount *acct,
							       ExchangeHierarchyType type);

void                   exchange_account_rescan_tree          (ExchangeAccount  *acct);

gchar		      *exchange_account_get_password	     (ExchangeAccount  *acct);

ExchangeAccountResult exchange_account_set_password	     (ExchangeAccount  *acct,
							      gchar             *old_password,
							      gchar             *new_password);

void		       exchange_account_forget_password       (ExchangeAccount  *acct);

void		       exchange_account_set_save_password    (ExchangeAccount *account,
							      gboolean save_password);

gboolean	       exchange_account_is_save_password     (ExchangeAccount *account);

gboolean	       exchange_account_set_offline          (ExchangeAccount  *account);

gboolean	       exchange_account_set_online           (ExchangeAccount  *account);

void		       exchange_account_is_offline           (ExchangeAccount  *account,
							      gint              *mode);

void		       exchange_account_is_offline_sync_set  (ExchangeAccount *account,
							      gint             *mode);

typedef enum {
	EXCHANGE_ACCOUNT_FOLDER_OK,
	EXCHANGE_ACCOUNT_FOLDER_ALREADY_EXISTS,
	EXCHANGE_ACCOUNT_FOLDER_DOES_NOT_EXIST,
	EXCHANGE_ACCOUNT_FOLDER_UNKNOWN_TYPE,
	EXCHANGE_ACCOUNT_FOLDER_PERMISSION_DENIED,
	EXCHANGE_ACCOUNT_FOLDER_OFFLINE,
	EXCHANGE_ACCOUNT_FOLDER_UNSUPPORTED_OPERATION,
	EXCHANGE_ACCOUNT_FOLDER_GC_NOTREACHABLE,
	EXCHANGE_ACCOUNT_FOLDER_NO_SUCH_USER,
	EXCHANGE_ACCOUNT_FOLDER_GENERIC_ERROR
} ExchangeAccountFolderResult;

ExchangeAccountFolderResult exchange_account_create_folder (ExchangeAccount *account,
							    const gchar      *path,
							    const gchar      *type);
ExchangeAccountFolderResult exchange_account_remove_folder (ExchangeAccount *account,
							    const gchar      *path);
ExchangeAccountFolderResult exchange_account_xfer_folder   (ExchangeAccount *account,
							    const gchar      *source_path,
							    const gchar      *dest_path,
							    gboolean         remove_source);
ExchangeAccountFolderResult exchange_account_open_folder   (ExchangeAccount *account,
							    const gchar      *path);

ExchangeAccountFolderResult exchange_account_discover_shared_folder  (ExchangeAccount *account,
								      const gchar      *user,
								      const gchar      *folder_name,
								      EFolder        **folder);
void       exchange_account_cancel_discover_shared_folder (ExchangeAccount *account,
							      const gchar      *user,
							      const gchar      *folder);
ExchangeAccountFolderResult exchange_account_remove_shared_folder    (ExchangeAccount *account,
								      const gchar      *path);

ExchangeAccountFolderResult exchange_account_add_favorite (ExchangeAccount *account,
							   EFolder         *folder);
ExchangeAccountFolderResult exchange_account_remove_favorite (ExchangeAccount *account,
							      EFolder         *folder);

gboolean exchange_account_is_favorite_folder              (ExchangeAccount *account,
							   EFolder         *folder);

gchar * exchange_account_get_username			  (ExchangeAccount *account);

gchar * exchange_account_get_windows_domain		  (ExchangeAccount *account);

gchar * exchange_account_get_email_id			  (ExchangeAccount *account);

gint exchange_account_get_quota_limit			  (ExchangeAccount *account);

gint exchange_account_check_password_expiry		  (ExchangeAccount *account);

/* Folder Size methods */
void			exchange_account_folder_size_add   (ExchangeAccount *account,
							     const gchar *folder_name,
							     gdouble size);
void			exchange_account_folder_size_remove (ExchangeAccount *account,
							     const gchar *folder_name);
void			exchange_account_folder_size_rename (ExchangeAccount *account,
							     const gchar *old_name,
							     const gchar *new_name);
GtkListStore	       *exchange_account_folder_size_get_model (ExchangeAccount *account);
void			exchange_account_scan_foreign_hierarchy (ExchangeAccount *account,
							      const gchar *user_email);

G_END_DECLS

#endif /* __EXCHANGE_ACCOUNT_H__ */
