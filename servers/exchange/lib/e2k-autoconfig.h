/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* Copyright (C) 2003, 2004 Novell, Inc. */

#ifndef __E2K_AUTOCONFIG_H__
#define __E2K_AUTOCONFIG_H__

#include "e2k-types.h"
#include "e2k-operation.h"

#ifdef __cplusplus
extern "C" {
#pragma }
#endif /* __cplusplus */

typedef enum {
	E2K_EXCHANGE_UNKNOWN,
	E2K_EXCHANGE_2000,
	E2K_EXCHANGE_2003,

	E2K_EXCHANGE_FUTURE
} E2kExchangeVersion;

typedef enum {
	E2K_AUTOCONFIG_USE_BASIC,
	E2K_AUTOCONFIG_USE_NTLM,
	E2K_AUTOCONFIG_USE_EITHER,
} E2kAutoconfigAuthPref;

typedef struct {
	/* Input data. (gc_server is optional) */
	char *owa_uri, *gc_server;
	char *username, *password;
	int gal_limit;

	/* Output data */
	E2kExchangeVersion version;
	char *display_name, *email;
	char *account_uri, *exchange_server;
	char *timezone;

	/* Private-ish members */
	char *nt_domain, *w2k_domain;
	char *home_uri, *exchange_dn;
	char *pf_server;
	E2kAutoconfigAuthPref auth_pref;
	gboolean require_ntlm, use_ntlm;
	gboolean saw_basic, saw_ntlm;
	gboolean nt_domain_defaulted, gc_server_autodetected;
} E2kAutoconfig;

typedef enum {
	E2K_AUTOCONFIG_OK,
	E2K_AUTOCONFIG_REDIRECT,
	E2K_AUTOCONFIG_TRY_SSL,
	E2K_AUTOCONFIG_AUTH_ERROR,
	E2K_AUTOCONFIG_AUTH_ERROR_TRY_DOMAIN,
	E2K_AUTOCONFIG_AUTH_ERROR_TRY_BASIC,
	E2K_AUTOCONFIG_AUTH_ERROR_TRY_NTLM,
	E2K_AUTOCONFIG_EXCHANGE_5_5,
	E2K_AUTOCONFIG_NOT_EXCHANGE,
	E2K_AUTOCONFIG_NO_OWA,
	E2K_AUTOCONFIG_NO_MAILBOX,
	E2K_AUTOCONFIG_CANT_BPROPFIND,
	E2K_AUTOCONFIG_CANT_RESOLVE,
	E2K_AUTOCONFIG_CANT_CONNECT,
	E2K_AUTOCONFIG_CANCELLED,
	E2K_AUTOCONFIG_FAILED
} E2kAutoconfigResult;

E2kAutoconfig       *e2k_autoconfig_new                  (const char *owa_uri,
							  const char *username,
							  const char *password,
							  E2kAutoconfigAuthPref auth_pref);
void                 e2k_autoconfig_free                 (E2kAutoconfig *ac);

void                 e2k_autoconfig_set_owa_uri          (E2kAutoconfig *ac,
							  const char *owa_uri);
void                 e2k_autoconfig_set_gc_server        (E2kAutoconfig *ac,
							  const char *gc_server,
							  int gal_limit);
void                 e2k_autoconfig_set_username         (E2kAutoconfig *ac,
							  const char *username);
void                 e2k_autoconfig_set_password         (E2kAutoconfig *ac,
							  const char *password);

E2kContext          *e2k_autoconfig_get_context          (E2kAutoconfig *ac,
							  E2kOperation *op,
							  E2kAutoconfigResult *result);
E2kAutoconfigResult  e2k_autoconfig_check_exchange       (E2kAutoconfig *ac,
							  E2kOperation *op);
E2kGlobalCatalog    *e2k_autoconfig_get_global_catalog   (E2kAutoconfig *ac,
							  E2kOperation *op);
E2kAutoconfigResult  e2k_autoconfig_check_global_catalog (E2kAutoconfig *ac,
							  E2kOperation *op);


const char          *e2k_autoconfig_lookup_option        (const char *option);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __EXCHANGE_AUTOCONFIG_H__ */
