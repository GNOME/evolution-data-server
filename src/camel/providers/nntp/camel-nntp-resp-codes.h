/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#ifndef CAMEL_NNTP_RESP_CODES_H
#define CAMEL_NNTP_RESP_CODES_H

#define CAMEL_NNTP_OK(x) ((x) < 400)
#define CAMEL_NNTP_ERR(x) (!CAMEL_NNTP_OK(x) && (x) < 500)
#define CAMEL_NNTP_FAIL(x) (!CAMEL_NNTP_OK(x) && !CAMEL_NNTP_ERR(x))

#define NNTP_GREETING_POSTING_OK    200
#define NNTP_GREETING_NO_POSTING    201

#define NNTP_EXTENSIONS_SUPPORTED     202
#define NNTP_GROUP_SELECTED           211
#define NNTP_LIST_FOLLOWS             215
#define NNTP_ARTICLE_FOLLOWS          220
#define NNTP_HEAD_FOLLOWS             221
#define NNTP_DATA_FOLLOWS             224
#define NNTP_NEW_ARTICLE_LIST_FOLLOWS 230
#define NNTP_NEW_GROUP_LIST_FOLLOWS   231

#define NNTP_NO_SUCH_GROUP          411
#define NNTP_NO_SUCH_ARTICLE        430

#define NNTP_NO_PERMISSION          502

/* authentication */
#define NNTP_AUTH_ACCEPTED          281
#define NNTP_AUTH_CONTINUE          381
#define NNTP_AUTH_REQUIRED          480
#define NNTP_AUTH_REJECTED          482

#define NNTP_PROTOCOL_ERROR         666

#endif /* CAMEL_NNTP_RESP_CODES_H */
