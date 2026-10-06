/*
 * SPDX-FileCopyrightText: (C) 2026 Tobias Mueller <muelli@cryptobitch.de>
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#if !defined (__LIBEDATASERVER_H_INSIDE__) && !defined (LIBEDATASERVER_COMPILATION)
#error "Only <libedataserver/libedataserver.h> should be included directly."
#endif

#ifndef E_OAUTH2_SERVICE_DYNAMIC_H
#define E_OAUTH2_SERVICE_DYNAMIC_H

#include <libedataserver/e-oauth2-service-base.h>

G_BEGIN_DECLS

#define E_TYPE_OAUTH2_SERVICE_DYNAMIC e_oauth2_service_dynamic_get_type ()

/**
 * EOAuth2ServiceDynamic:
 * Since: 3.64
 **/
G_DECLARE_FINAL_TYPE (EOAuth2ServiceDynamic, e_oauth2_service_dynamic, E, OAUTH2_SERVICE_DYNAMIC, EOAuth2ServiceBase)

G_END_DECLS

#endif /* E_OAUTH2_SERVICE_DYNAMIC_H */
