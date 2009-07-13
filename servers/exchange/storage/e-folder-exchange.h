/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* Copyright (C) 2001-2004 Novell, Inc. */

#ifndef __E_FOLDER_EXCHANGE_H__
#define __E_FOLDER_EXCHANGE_H__

#include "e-folder.h"
#include "exchange-types.h"
#include "e2k-context.h"

G_BEGIN_DECLS

#define E_TYPE_FOLDER_EXCHANGE            (e_folder_exchange_get_type ())
#define E_FOLDER_EXCHANGE(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), E_TYPE_FOLDER_EXCHANGE, EFolderExchange))
#define E_FOLDER_EXCHANGE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), E_TYPE_FOLDER_EXCHANGE, EFolderExchangeClass))
#define E_IS_FOLDER_EXCHANGE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), E_TYPE_FOLDER_EXCHANGE))
#define E_IS_FOLDER_EXCHANGE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((obj), E_TYPE_FOLDER_EXCHANGE))

struct _EFolderExchange {
	EFolder parent;

	EFolderExchangePrivate *priv;
};

struct _EFolderExchangeClass {
	EFolderClass parent_class;

};

GType       e_folder_exchange_get_type      (void);

EFolder    *e_folder_exchange_new           (ExchangeHierarchy     *hier,
					     const gchar            *name,
					     const gchar            *type,
					     const gchar            *outlook_class,
					     const gchar            *phys_uri,
					     const gchar            *int_uri);

EFolder    *e_folder_exchange_new_from_file (ExchangeHierarchy     *hier,
					     const gchar            *filename);
gboolean    e_folder_exchange_save_to_file  (EFolder               *folder,
					     const gchar            *filename);

const gchar *e_folder_exchange_get_internal_uri     (EFolder    *folder);
void        e_folder_exchange_set_internal_uri     (EFolder    *folder,
						    const gchar *internal_uri);

const gchar *e_folder_exchange_get_path             (EFolder    *folder);

const gchar *e_folder_exchange_get_permanent_uri    (EFolder    *folder);
void        e_folder_exchange_set_permanent_uri    (EFolder    *folder,
						    const gchar *permanent_uri);

gint64	e_folder_exchange_get_folder_size (EFolder *folder);
void		e_folder_exchange_set_folder_size (EFolder *folder, gint64 folder_size);

gboolean    e_folder_exchange_get_has_subfolders   (EFolder    *folder);
void        e_folder_exchange_set_has_subfolders   (EFolder    *folder,
						    gboolean   has_subfolders);

gboolean    e_folder_exchange_get_rescan_tree   (EFolder    *folder);
void        e_folder_exchange_set_rescan_tree   (EFolder    *folder,
						 gboolean   has_subfolders);

const gchar *e_folder_exchange_get_outlook_class    (EFolder    *folder);

gchar       *e_folder_exchange_get_storage_file     (EFolder    *folder,
						    const gchar *filename);

ExchangeHierarchy *e_folder_exchange_get_hierarchy (EFolder    *folder);

/* E2kContext wrappers */
E2kHTTPStatus  e_folder_exchange_propfind          (EFolder *folder,
						    E2kOperation *op,
						    const gchar **props,
						    gint nprops,
						    E2kResult **results,
						    gint *nresults);
E2kResultIter *e_folder_exchange_bpropfind_start   (EFolder *folder,
						    E2kOperation *op,
						    const gchar **hrefs,
						    gint nhrefs,
						    const gchar **props,
						    gint nprops);

E2kResultIter *e_folder_exchange_search_start      (EFolder *folder,
						    E2kOperation *op,
						    const gchar **props,
						    gint nprops,
						    E2kRestriction *rn,
						    const gchar *orderby,
						    gboolean ascending);

void           e_folder_exchange_subscribe         (EFolder *folder,
						    E2kContextChangeType,
						    gint min_interval,
						    E2kContextChangeCallback,
						    gpointer user_data);
void           e_folder_exchange_unsubscribe       (EFolder *folder);

E2kResultIter *e_folder_exchange_transfer_start    (EFolder *source,
						    E2kOperation *op,
						    EFolder *dest,
						    GPtrArray *source_hrefs,
						    gboolean delete_originals);

E2kHTTPStatus  e_folder_exchange_put_new           (EFolder *folder,
						    E2kOperation *op,
						    const gchar *object_name,
						    E2kContextTestCallback,
						    gpointer user_data,
						    const gchar *content_type,
						    const gchar *body,
						    gint length,
						    gchar **location,
						    gchar **repl_uid);

E2kHTTPStatus  e_folder_exchange_proppatch_new     (EFolder *folder,
						    E2kOperation *op,
						    const gchar *object_name,
						    E2kContextTestCallback,
						    gpointer user_data,
						    E2kProperties *props,
						    gchar **location,
						    gchar **repl_uid);

E2kResultIter *e_folder_exchange_bproppatch_start  (EFolder *folder,
						    E2kOperation *op,
						    const gchar **hrefs,
						    gint nhrefs,
						    E2kProperties *props,
						    gboolean create);

E2kResultIter *e_folder_exchange_bdelete_start     (EFolder *folder,
						    E2kOperation *op,
						    const gchar **hrefs,
						    gint nhrefs);

E2kHTTPStatus  e_folder_exchange_mkcol             (EFolder *folder,
						    E2kOperation *op,
						    E2kProperties *props,
						    gchar **permanent_url);
E2kHTTPStatus  e_folder_exchange_delete            (EFolder *folder,
						    E2kOperation *op);
E2kHTTPStatus  e_folder_exchange_transfer_dir      (EFolder *source,
						    E2kOperation *op,
						    EFolder *dest,
						    gboolean delete_original,
						    gchar **permanent_url);

G_END_DECLS

#endif /* __E_FOLDER_EXCHANGE_H__ */
