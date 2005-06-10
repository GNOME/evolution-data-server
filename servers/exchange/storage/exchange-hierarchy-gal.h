/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* Copyright (C) 2001-2004 Novell, Inc. */

#ifndef __EXCHANGE_HIERARCHY_GAL_H__
#define __EXCHANGE_HIERARCHY_GAL_H__

#include "exchange-hierarchy.h"

#ifdef __cplusplus
extern "C" {
#pragma }
#endif /* __cplusplus */

#define EXCHANGE_TYPE_HIERARCHY_GAL            (exchange_hierarchy_gal_get_type ())
#define EXCHANGE_HIERARCHY_GAL(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), EXCHANGE_TYPE_HIERARCHY_GAL, ExchangeHierarchyGAL))
#define EXCHANGE_HIERARCHY_GAL_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), EXCHANGE_TYPE_HIERARCHY_GAL, ExchangeHierarchyGALClass))
#define EXCHANGE_IS_HIERARCHY_GAL(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), EXCHANGE_TYPE_HIERARCHY_GAL))
#define EXCHANGE_IS_HIERARCHY_GAL_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((obj), EXCHANGE_TYPE_HIERARCHY_GAL))

struct _ExchangeHierarchyGAL {
	ExchangeHierarchy parent;

};

struct _ExchangeHierarchyGALClass {
	ExchangeHierarchyClass parent_class;

};

GType                 exchange_hierarchy_gal_get_type (void);

ExchangeHierarchy    *exchange_hierarchy_gal_new      (ExchangeAccount *account,
						       const char *hierarchy_name,
						       const char *physical_uri_prefix);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __EXCHANGE_HIERARCHY_GAL_H__ */
