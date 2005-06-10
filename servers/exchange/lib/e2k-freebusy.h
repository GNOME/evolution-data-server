/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* Copyright (C) 2002-2004 Novell, Inc. */

#ifndef __E2K_FREEBUSY_H__
#define __E2K_FREEBUSY_H__

#include "e2k-context.h"

#ifdef __cplusplus
extern "C" {
#pragma }
#endif /* __cplusplus */

typedef enum {
	E2K_BUSYSTATUS_FREE = 0,
	E2K_BUSYSTATUS_TENTATIVE = 1,
	E2K_BUSYSTATUS_BUSY = 2,
	E2K_BUSYSTATUS_OOF = 3,

	E2K_BUSYSTATUS_MAX,

	/* Alias for internal use */
	E2K_BUSYSTATUS_ALL = E2K_BUSYSTATUS_FREE
} E2kBusyStatus;

typedef struct {
	/*< private >*/
	time_t start, end;
} E2kFreebusyEvent;

typedef struct {
	/*< private >*/
	E2kContext *ctx;
	char *dn, *uri;

	time_t start, end;

	GArray *events[E2K_BUSYSTATUS_MAX];
} E2kFreebusy;

E2kFreebusy   *e2k_freebusy_new                   (E2kContext      *ctx,
						   const char      *public_uri,
						   const char      *dn);

void           e2k_freebusy_reset                 (E2kFreebusy     *fb,
						   int              nmonths);

void           e2k_freebusy_add_interval          (E2kFreebusy     *fb,
						   E2kBusyStatus    busystatus,
						   time_t           start,
						   time_t           end);
void           e2k_freebusy_clear_interval        (E2kFreebusy     *fb,
						   time_t           start,
						   time_t           end);

E2kHTTPStatus  e2k_freebusy_add_from_calendar_uri (E2kFreebusy     *fb,
						   const char      *uri,
						   time_t           start_tt,
						   time_t           end_tt);

E2kHTTPStatus  e2k_freebusy_save                  (E2kFreebusy     *fb);

void           e2k_freebusy_destroy               (E2kFreebusy     *fb);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __E2K_FREEBUSY_H__ */
