/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* Copyright (C) 2003, 2004 Novell, Inc. */

#ifndef __E2K_VALIDATE_H_
#define __E2K_VALIDATE_H_

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

gboolean e2k_validate_user (const char *owa_url, char *user, ExchangeParams *exchange_params, gboolean *remember_password);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __EXCHANGE_VALIDATE_H_ */
