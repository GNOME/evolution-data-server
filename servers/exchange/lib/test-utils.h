/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* Copyright (C) 2001-2004 Novell, Inc. */

#ifndef TEST_UTILS_H
#define TEST_UTILS_H

#include "e2k-types.h"
#include "e2k-http-utils.h"

void              test_main                (gint             argc,
					    gchar          **argv);
void              test_quit                (void);

const gchar       *test_get_password        (const gchar     *user,
					    const gchar     *host);
E2kContext       *test_get_context         (const gchar     *uri);
E2kGlobalCatalog *test_get_gc              (const gchar     *server);

void              test_abort_if_http_error (E2kHTTPStatus   status);

/* lower-level util */
gchar             *test_ask_password        (const gchar     *prompt);

#endif /* TEST_UTILS_H */
