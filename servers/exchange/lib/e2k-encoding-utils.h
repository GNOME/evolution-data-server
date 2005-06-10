/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* Copyright (C) 2001-2004 Novell, Inc. */

#ifndef E2K_ENCODING_UTILS_H
#define E2K_ENCODING_UTILS_H

#include <glib.h>

char       *e2k_base64_encode (const guint8 *data, int len); 
GByteArray *e2k_base64_decode (const char *string);

#endif /* E2K_ENCODING_UTILS_H */
