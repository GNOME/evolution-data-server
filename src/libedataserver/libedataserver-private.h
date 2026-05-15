/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 * SPDX-FileContributor: Tor Lillqvist <tml@novell.com>
 */

#ifndef LIBEDATASERVER_PRIVATE_H
#define LIBEDATASERVER_PRIVATE_H

#ifdef G_OS_WIN32

const gchar *	_libedataserver_get_imagesdir		(void) G_GNUC_CONST;
const gchar *	_libedataserver_get_credentialmoduledir	(void) G_GNUC_CONST;
const gchar *	_libedataserver_get_uimoduledir		(void) G_GNUC_CONST;

#undef E_DATA_SERVER_IMAGESDIR
#define E_DATA_SERVER_IMAGESDIR _libedataserver_get_imagesdir ()

#undef E_DATA_SERVER_CREDENTIALMODULEDIR
#define E_DATA_SERVER_CREDENTIALMODULEDIR _libedataserver_get_credentialmoduledir ()

#undef E_DATA_SERVER_UIMODULEDIR
#define E_DATA_SERVER_UIMODULEDIR _libedataserver_get_uimoduledir ()

#endif	/* G_OS_WIN32 */

#endif	/* LIBEDATASERVER_PRIVATE_H */
