/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* Copyright (C) 2001-2004 Novell, Inc. */

#ifndef __EXCHANGE_ESOURCE_H__
#define __EXCHANGE_ESOURCE_H__

#include "exchange-constants.h"
#include "exchange-account.h"

#ifdef __cplusplus
extern "C" {
#pragma }
#endif /* __cplusplus */

#define CONF_KEY_SELECTED_CAL_SOURCES "/apps/evolution/calendar/display/selected_calendars"
#define CONF_KEY_SELECTED_TASKS_SOURCES "/apps/evolution/calendar/tasks/selected_tasks"
#define CONF_KEY_CAL "/apps/evolution/calendar/sources"
#define CONF_KEY_TASKS "/apps/evolution/tasks/sources"
#define CONF_KEY_CONTACTS "/apps/evolution/addressbook/sources"
#define EXCHANGE_URI_PREFIX "exchange://"

void 			add_folder_esource (ExchangeAccount *account, FolderType folder_type, const char *folder_name, const char *physical_uri);
void 			remove_folder_esource (ExchangeAccount *account, FolderType folder_type, const char *physical_uri);

/* Remove this ugly hack by moving this to exchange-account.h */
char * exchange_account_get_authtype (ExchangeAccount *account);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __EXCHANGE_ESOURCE_H__ */
