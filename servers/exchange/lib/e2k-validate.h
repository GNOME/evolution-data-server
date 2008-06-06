/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* Copyright (C) 2003, 2004 Novell, Inc. */

#ifndef __E2K_VALIDATE_H_
#define __E2K_VALIDATE_H_

#include <gtk/gtk.h>

#ifdef __cplusplus
extern "C" {
#pragma }
#endif /* __cplusplus */

typedef struct {
        char *host;
        char *ad_server;
        char *mailbox;
        char *owa_path;
	gboolean is_ntlm;
}ExchangeParams;

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

gboolean e2k_validate_user (const char *owa_url, char *key, char **user,
			    ExchangeParams *exchange_params,
			    gboolean *remember_password,
			    E2kAutoconfigResult *result,
			    GtkWindow *parent);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __EXCHANGE_VALIDATE_H_ */
