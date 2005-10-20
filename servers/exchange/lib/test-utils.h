/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* Copyright (C) 2001-2004 Novell, Inc. */

#ifndef TEST_UTILS_H
#define TEST_UTILS_H

#include "e2k-types.h"
#include "e2k-http-utils.h"

void              test_main                (int             argc,
					    char          **argv);
void              test_quit                (void);

const char       *test_get_password        (const char     *user,
					    const char     *host);
E2kContext       *test_get_context         (const char     *uri);
E2kGlobalCatalog *test_get_gc              (const char     *server);

void              test_abort_if_http_error (E2kHTTPStatus   status);


/* lower-level util */
char             *test_ask_password        (const char     *prompt);

#endif /* TEST_UTILS_H */
