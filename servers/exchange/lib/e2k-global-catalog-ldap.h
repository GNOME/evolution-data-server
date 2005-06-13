/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* Copyright (C) 2001-2004 Novell, Inc. */

#ifndef __E2K_GLOBAL_CATALOG_LDAP_H__
#define __E2K_GLOBAL_CATALOG_LDAP_H__

#include <ldap.h>
#include "e2k-global-catalog.h"

#ifdef __cplusplus
extern "C" {
#pragma }
#endif /* __cplusplus */

LDAP             *e2k_global_catalog_get_ldap        (E2kGlobalCatalog *gc,
						      E2kOperation     *op);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __E2K_GLOBAL_CATALOG_LDAP_H__ */
