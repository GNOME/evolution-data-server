/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* Copyright (C) 2001-2004 Novell, Inc. */

#ifndef __E2K_GLOBAL_CATALOG_H__
#define __E2K_GLOBAL_CATALOG_H__

#include <glib-object.h>
#include "e2k-types.h"
#include "e2k-operation.h"
#include "e2k-validate.h"

G_BEGIN_DECLS

#define E2K_TYPE_GLOBAL_CATALOG            (e2k_global_catalog_get_type ())
#define E2K_GLOBAL_CATALOG(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), E2K_TYPE_GLOBAL_CATALOG, E2kGlobalCatalog))
#define E2K_GLOBAL_CATALOG_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), E2K_TYPE_GLOBAL_CATALOG, E2kGlobalCatalogClass))
#define E2K_IS_GLOBAL_CATALOG(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), E2K_TYPE_GLOBAL_CATALOG))
#define E2K_IS_GLOBAL_CATALOG_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((obj), E2K_TYPE_GLOBAL_CATALOG))

struct _E2kGlobalCatalog {
	GObject parent;

	gchar *domain;
	gint response_limit;

	E2kGlobalCatalogPrivate *priv;
};

struct _E2kGlobalCatalogClass {
	GObjectClass parent_class;

};

GType             e2k_global_catalog_get_type        (void);
E2kGlobalCatalog *e2k_global_catalog_new             (const gchar *server,
						      gint response_limit,
						      const gchar *user,
						      const gchar *domain,
						      const gchar *password,
						      E2kAutoconfigGalAuthPref use_auth);

typedef enum {
	E2K_GLOBAL_CATALOG_OK,
	E2K_GLOBAL_CATALOG_NO_SUCH_USER,
	E2K_GLOBAL_CATALOG_NO_DATA,
	E2K_GLOBAL_CATALOG_BAD_DATA,
	E2K_GLOBAL_CATALOG_EXISTS,
	E2K_GLOBAL_CATALOG_AUTH_FAILED,
	E2K_GLOBAL_CATALOG_CANCELLED,
	E2K_GLOBAL_CATALOG_ERROR
} E2kGlobalCatalogStatus;

typedef enum {
	E2K_GLOBAL_CATALOG_LOOKUP_BY_EMAIL,
	E2K_GLOBAL_CATALOG_LOOKUP_BY_DN,
	E2K_GLOBAL_CATALOG_LOOKUP_BY_LEGACY_EXCHANGE_DN
} E2kGlobalCatalogLookupType;

typedef enum {
	E2K_GLOBAL_CATALOG_LOOKUP_SID                = (1 << 0),
	E2K_GLOBAL_CATALOG_LOOKUP_EMAIL              = (1 << 1),
	E2K_GLOBAL_CATALOG_LOOKUP_MAILBOX            = (1 << 2),
	E2K_GLOBAL_CATALOG_LOOKUP_LEGACY_EXCHANGE_DN = (1 << 3),
	E2K_GLOBAL_CATALOG_LOOKUP_DELEGATES          = (1 << 4),
	E2K_GLOBAL_CATALOG_LOOKUP_DELEGATORS         = (1 << 5),
	E2K_GLOBAL_CATALOG_LOOKUP_QUOTA		     = (1 << 6),
	E2K_GLOBAL_CATALOG_LOOKUP_ACCOUNT_CONTROL    = (1 << 7)
} E2kGlobalCatalogLookupFlags;

typedef struct {
	gchar *dn, *display_name;
	E2kSid *sid;
	gchar *email, *exchange_server, *mailbox, *legacy_exchange_dn;
	GPtrArray *delegates, *delegators;
	gint quota_warn, quota_nosend, quota_norecv;
	gint user_account_control;

	E2kGlobalCatalogLookupFlags mask;
} E2kGlobalCatalogEntry;

E2kGlobalCatalogStatus e2k_global_catalog_lookup (E2kGlobalCatalog *gc,
						  E2kOperation     *op,
						  E2kGlobalCatalogLookupType type,
						  const gchar *key,
						  E2kGlobalCatalogLookupFlags flags,
						  E2kGlobalCatalogEntry **entry_p);

typedef void         (*E2kGlobalCatalogCallback) (E2kGlobalCatalog *gc,
						  E2kGlobalCatalogStatus status,
						  E2kGlobalCatalogEntry *entry,
						  gpointer user_data);

void             e2k_global_catalog_async_lookup (E2kGlobalCatalog *gc,
						  E2kOperation     *op,
						  E2kGlobalCatalogLookupType type,
						  const gchar *key,
						  E2kGlobalCatalogLookupFlags flags,
						  E2kGlobalCatalogCallback callback,
						  gpointer user_data);

gdouble		lookup_passwd_max_age (E2kGlobalCatalog *gc,
				      E2kOperation *op);

#define e2k_global_catalog_entry_free(gc, entry)

E2kGlobalCatalogStatus e2k_global_catalog_add_delegate    (E2kGlobalCatalog *gc,
							   E2kOperation     *op,
							   const gchar *self_dn,
							   const gchar *delegate_dn);
E2kGlobalCatalogStatus e2k_global_catalog_remove_delegate (E2kGlobalCatalog *gc,
							   E2kOperation     *op,
							   const gchar *self_dn,
							   const gchar *delegate_dn);

G_END_DECLS

#endif /* __E2K_GLOBAL_CATALOG_H__ */
