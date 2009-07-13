/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* Copyright (C) 2002-2004 Novell, Inc. */

#ifndef __EXCHANGE_FOLDER_SIZE_H__
#define __EXCHANGE_FOLDER_SIZE_H__

#include "exchange-types.h"
#include "e2k-security-descriptor.h"
#include <gtk/gtk.h>

G_BEGIN_DECLS

#define EXCHANGE_TYPE_FOLDER_SIZE			(exchange_folder_size_get_type ())
#define EXCHANGE_FOLDER_SIZE(obj)			(G_TYPE_CHECK_INSTANCE_CAST ((obj), EXCHANGE_TYPE_FOLDER_SIZE, ExchangeFolderSize))
#define EXCHANGE_FOLDER_SIZE_CLASS(klass)		(G_TYPE_CHECK_CLASS_CAST ((klass), EXCHANGE_TYPE_FOLDER_SIZE, ExchangeFolderSizeClass))
#define EXCHANGE_IS_FOLDER_SIZE(obj)			(G_TYPE_CHECK_INSTANCE_TYPE ((obj), EXCHANGE_TYPE_FOLDER_SIZE))
#define EXCHANGE_IS_FOLDER_SIZE_CLASS(klass)		(G_TYPE_CHECK_CLASS_TYPE ((obj), EXCHANGE_TYPE_FOLDER_SIZE))

typedef struct _ExchangeFolderSize        ExchangeFolderSize;
typedef struct _ExchangeFolderSizePrivate ExchangeFolderSizePrivate;
typedef struct _ExchangeFolderSizeClass   ExchangeFolderSizeClass;

struct _ExchangeFolderSize {
	GObject parent;

	ExchangeFolderSizePrivate *priv;
};

struct _ExchangeFolderSizeClass {
	GObjectClass parent_class;

};

GType    exchange_folder_size_get_type (void);

ExchangeFolderSize *exchange_folder_size_new (void);

void exchange_folder_size_update (ExchangeFolderSize *fsize,
						const gchar *folder_name,
						gdouble folder_size);
void exchange_folder_size_remove (ExchangeFolderSize *fsize, const gchar *folder_name);

gdouble exchange_folder_size_get (ExchangeFolderSize *fsize, const gchar *folder_name);

GtkListStore *exchange_folder_size_get_model (ExchangeFolderSize *fsize);

G_END_DECLS

#endif /* __EXCHANGE_FOLDER_SIZE_H__ */
