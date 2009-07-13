/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* Copyright (C) 2001-2004 Novell, Inc. */

#ifndef __EXCHANGE_HIERARCHY_SOMEDAV_H__
#define __EXCHANGE_HIERARCHY_SOMEDAV_H__

#include "exchange-hierarchy-webdav.h"

G_BEGIN_DECLS

#define EXCHANGE_TYPE_HIERARCHY_SOMEDAV            (exchange_hierarchy_somedav_get_type ())
#define EXCHANGE_HIERARCHY_SOMEDAV(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), EXCHANGE_TYPE_HIERARCHY_SOMEDAV, ExchangeHierarchySomeDAV))
#define EXCHANGE_HIERARCHY_SOMEDAV_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), EXCHANGE_TYPE_HIERARCHY_SOMEDAV, ExchangeHierarchySomeDAVClass))
#define EXCHANGE_IS_HIERARCHY_SOMEDAV(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), EXCHANGE_TYPE_HIERARCHY_SOMEDAV))
#define EXCHANGE_IS_HIERARCHY_SOMEDAV_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((obj), EXCHANGE_TYPE_HIERARCHY_SOMEDAV))
#define EXCHANGE_GET_HIERARCHY_SOMEDAV_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), EXCHANGE_TYPE_HIERARCHY_SOMEDAV, ExchangeHierarchySomeDAVClass))

struct _ExchangeHierarchySomeDAV {
	ExchangeHierarchyWebDAV parent;

	ExchangeHierarchySomeDAVPrivate *priv;
};

struct _ExchangeHierarchySomeDAVClass {
	ExchangeHierarchyWebDAVClass parent_class;

	/* signals */
	void (*href_unreadable) (ExchangeHierarchySomeDAV *hsd, const gchar *href);

	/* methods */
	GPtrArray *(*get_hrefs) (ExchangeHierarchySomeDAV *hsd);
};

GType exchange_hierarchy_somedav_get_type (void);

GPtrArray *exchange_hierarchy_somedav_get_hrefs (ExchangeHierarchySomeDAV *hsd);
ExchangeAccountFolderResult exchange_hierarchy_somedav_add_folder (ExchangeHierarchySomeDAV *hsd,
								   const gchar *uri);

/* signal emitter */
void exchange_hierarchy_somedav_href_unreadable (ExchangeHierarchySomeDAV *hsd,
						 const gchar *href);

G_END_DECLS

#endif /* __EXCHANGE_HIERARCHY_SOMEDAV_H__ */
