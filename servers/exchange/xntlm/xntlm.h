/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* Copyright (C) 2001-2004 Novell, Inc. */

#ifndef _XNTLM_H
#define _XNTLM_H

#include <glib.h>

GByteArray *xntlm_negotiate       (void);
gboolean    xntlm_parse_challenge (gpointer challenge, int len, char **nonce,
				   char **nt_domain, char **w2k_domain);
GByteArray *xntlm_authenticate    (const char *nonce, const char *domain,
				   const char *user, const char *password,
				   const char *workstation);

#endif /* _XNTLM_H */
