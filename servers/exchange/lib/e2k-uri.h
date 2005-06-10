/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* Copyright (C) 2001-2004 Novell, Inc. */

#ifndef E2K_URI_H
#define E2K_URI_H

#include <glib.h>

typedef struct {
	char  *protocol;
	char  *user;
	char  *domain;
	char  *authmech;
	char  *passwd;
	char  *host;
	int    port;
	char  *path;
	GData *params;
	char  *query;
	char  *fragment;
} E2kUri;


E2kUri *    e2k_uri_new       (const char *uri_string);
void        e2k_uri_free      (E2kUri *uri);
const char *e2k_uri_get_param (E2kUri *uri, const char *name);

void        e2k_uri_decode         (char *part);
char *      e2k_uri_encode         (const char *in,
				    gboolean    wss_encode,
				    const char *extra_enc_chars);
void        e2k_uri_append_encoded (GString    *str,
				    const char *in,
				    gboolean    wss_encode,
				    const char *extra_enc_chars);

const char *e2k_uri_path      (const char *uri_string);

char       *e2k_uri_concat    (const char *uri_prefix, const char *tail);
const char *e2k_uri_relative  (const char *uri_prefix, const char *uri);

#endif /* E2K_URI_H */
