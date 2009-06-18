/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* Copyright (C) 2001-2004 Novell, Inc. */

#ifndef __E2K_GLOBAL_CATALOG_LDAP_H__
#define __E2K_GLOBAL_CATALOG_LDAP_H__

#include <glib.h>
#ifndef G_OS_WIN32
#include <ldap.h>
#else
#define interface windows_interface
#include <windows.h>
#undef interface
#include <winldap.h>
#define LDAP_ROOT_DSE		""
#define LDAP_RANGE(n,x,y)	(((x) <= (n)) && ((n) <= (y)))
#define LDAP_NAME_ERROR(n)	LDAP_RANGE((n),0x20,0x24) /* 32-34,36 */
#define ldap_msgtype(lm)	(lm)->lm_msgtype
#define ldap_msgid(lm)		(lm)->lm_msgid
#endif
#include "e2k-global-catalog.h"

G_BEGIN_DECLS

LDAP             *e2k_global_catalog_get_ldap        (E2kGlobalCatalog *gc,
						      E2kOperation     *op,
						      gint              *ldap_error);

G_END_DECLS

#endif /* __E2K_GLOBAL_CATALOG_LDAP_H__ */
