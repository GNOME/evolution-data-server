/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* Copyright (C) 2001-2004 Novell, Inc. */

#ifndef __EXCHANGE_CONSTANTS_H__
#define __EXCHANGE_CONSTANTS_H__


enum {
	UNSUPPORTED_MODE = 0,
        OFFLINE_MODE,
        ONLINE_MODE
};

typedef enum {
	EXCHANGE_CALENDAR_FOLDER,
	EXCHANGE_TASKS_FOLDER,
	EXCHANGE_CONTACTS_FOLDER
}FolderType;


#define EXCHANGE_COMPONENT_FACTORY_IID  "OAFIID:GNOME_Evolution_Exchange_Component_Factory:" BASE_VERSION
#define EXCHANGE_COMPONENT_IID		"OAFIID:GNOME_Evolution_Exchange_Component:" BASE_VERSION
#define EXCHANGE_CALENDAR_FACTORY_ID	"OAFIID:GNOME_Evolution_Exchange_Connector_CalFactory:" BASE_VERSION
#define EXCHANGE_ADDRESSBOOK_FACTORY_ID	"OAFIID:GNOME_Evolution_Exchange_Connector_BookFactory:" BASE_VERSION
#define EXCHANGE_AUTOCONFIG_WIZARD_ID	"OAFIID:GNOME_Evolution_Exchange_Connector_Startup_Wizard:" BASE_VERSION

#endif
