/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Blanket header containing the typedefs for object types used in the
 * PAS stuff, so we can disentangle the #includes.
 *
 * Author: Chris Toshok <toshok@ximian.com>
 *
 * Copyright 2003, Ximian, Inc.
 */

#ifndef __E_DATA_BOOK_TYPES_H__
#define __E_DATA_BOOK_TYPES_H__

typedef struct _EDataBookView        EDataBookView;
typedef struct _EDataBookViewClass   EDataBookViewClass;

typedef struct _EBookBackendSExp EBookBackendSExp;
typedef struct _EBookBackendSExpClass EBookBackendSExpClass;

typedef struct _EBookBackend        EBookBackend;
typedef struct _EBookBackendClass   EBookBackendClass;

typedef struct _EBookBackendSummary EBookBackendSummary;
typedef struct _EBookBackendSummaryClass EBookBackendSummaryClass;

typedef struct _EBookBackendSync        EBookBackendSync;
typedef struct _EBookBackendSyncClass   EBookBackendSyncClass;

typedef struct _EDataBook        EDataBook;
typedef struct _EDataBookClass   EDataBookClass;

#endif /* __E_DATA_BOOK_TYPES_H__ */
