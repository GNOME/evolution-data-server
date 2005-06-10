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

#ifdef __cplusplus
extern "C" {
#pragma }
#endif /* __cplusplus */

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
	char *account_name, *account_filename, *storage_dir;
	char *exchange_server, *home_uri, *public_uri;
	char *legacy_exchange_dn, *default_timezone;

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

#if 0
enum {
	UNSUPPORTED_MODE = 0,
        OFFLINE_MODE,
        ONLINE_MODE
};
#endif

GType                  exchange_account_get_type             (void);
ExchangeAccount       *exchange_account_new                  (EAccountList                   *account_list,
							      EAccount                       *adata);
E2kContext            *exchange_account_get_context          (ExchangeAccount                *acct);
E2kGlobalCatalog      *exchange_account_get_global_catalog   (ExchangeAccount                *acct);

const char            *exchange_account_get_standard_uri     (ExchangeAccount                *acct,
							      const char                     *item);

char                  *exchange_account_get_standard_uri_for (ExchangeAccount                *acct,
							      const char                     *home_uri,
							      const char                     *std_uri_prop);
char                  *exchange_account_get_foreign_uri      (ExchangeAccount                *acct,
							      E2kGlobalCatalogEntry          *entry,
							      const char                     *std_uri_prop);

E2kContext            *exchange_account_connect              (ExchangeAccount                *acct);

EFolder               *exchange_account_get_folder           (ExchangeAccount                *acct,
							      const char                     *path_or_uri);
GPtrArray             *exchange_account_get_folders          (ExchangeAccount                *acct);

void                   exchange_account_rescan_tree          (ExchangeAccount                *acct);

char 		       *exchange_account_get_password (ExchangeAccount *acct);

void		       exchange_account_set_password (ExchangeAccount *acct,
							char *old_password,
							char *new_password);
void 		      exchange_account_forget_password (ExchangeAccount *acct);

gboolean		 exchange_account_set_offline (ExchangeAccount *account);

gboolean		 exchange_account_set_online (ExchangeAccount *account);

void		 exchange_account_is_offline (ExchangeAccount *account, int *mode);

void		exchange_account_is_offline_sync_set (ExchangeAccount *account, int *mode);


typedef enum {
	EXCHANGE_ACCOUNT_FOLDER_OK,
	EXCHANGE_ACCOUNT_FOLDER_ALREADY_EXISTS,
	EXCHANGE_ACCOUNT_FOLDER_DOES_NOT_EXIST,
	EXCHANGE_ACCOUNT_FOLDER_UNKNOWN_TYPE,
	EXCHANGE_ACCOUNT_FOLDER_PERMISSION_DENIED,
	EXCHANGE_ACCOUNT_FOLDER_OFFLINE,
	EXCHANGE_ACCOUNT_FOLDER_UNSUPPORTED_OPERATION,
	EXCHANGE_ACCOUNT_FOLDER_GENERIC_ERROR
} ExchangeAccountFolderResult;

ExchangeAccountFolderResult exchange_account_create_folder (ExchangeAccount *account,
							    const char      *path,
							    const char      *type);
ExchangeAccountFolderResult exchange_account_remove_folder (ExchangeAccount *account,
							    const char      *path);
ExchangeAccountFolderResult exchange_account_xfer_folder   (ExchangeAccount *account,
							    const char      *source_path,
							    const char      *dest_path,
							    gboolean         remove_source);
ExchangeAccountFolderResult exchange_account_open_folder   (ExchangeAccount *account,
							    const char      *path);

ExchangeAccountFolderResult exchange_account_discover_shared_folder  (ExchangeAccount *account,
								      const char      *user,
								      const char      *folder_name,
								      EFolder        **folder);
void                  exchange_account_cancel_discover_shared_folder (ExchangeAccount *account,
								      const char      *user,
								      const char      *folder);
ExchangeAccountFolderResult exchange_account_remove_shared_folder    (ExchangeAccount *account,
								      const char      *path);

ExchangeAccountFolderResult exchange_account_add_favorite            (ExchangeAccount *account,
								      EFolder         *folder);
ExchangeAccountFolderResult exchange_account_remove_favorite         (ExchangeAccount *account,
								      EFolder         *folder);

char * exchange_account_get_username (ExchangeAccount *account);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __EXCHANGE_ACCOUNT_H__ */
