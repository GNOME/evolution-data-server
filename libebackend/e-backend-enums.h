/*
 * e-backend-enums.h
 *
 * This library is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 */

#if !defined (__LIBEBACKEND_H_INSIDE__) && !defined (LIBEBACKEND_COMPILATION)
#error "Only <libebackend/libebackend.h> should be included directly."
#endif

#ifndef E_BACKEND_ENUMS_H
#define E_BACKEND_ENUMS_H

/**
 * EAuthenticationSessionResult:
 * @E_AUTHENTICATION_SESSION_ERROR:
 *   An error occurred while authenticating.
 * @E_AUTHENTICATION_SESSION_SUCCESS:
 *   Client reported successful authentication.
 * @E_AUTHENTICATION_SESSION_DISMISSED:
 *   User dismissed the authentication prompt.
 *
 * Completion codes used by #EAuthenticationSession.
 *
 * Since: 3.6
 **/
typedef enum {
	E_AUTHENTICATION_SESSION_ERROR,
	E_AUTHENTICATION_SESSION_SUCCESS,
	E_AUTHENTICATION_SESSION_DISMISSED
} EAuthenticationSessionResult;

/**
 * EDBusServerExitCode:
 * @E_DBUS_SERVER_EXIT_NONE:
 *   The server's run state is unchanged.
 * @E_DBUS_SERVER_EXIT_NORMAL:
 *   Normal termination.  The process itself may now terminate.
 * @E_DBUS_SERVER_EXIT_RELOAD:
 *   The server should reload its configuration and start again.
 *   Servers that do not support reloading may wish to intercept
 *   this exit code and stop the #EDBusServer::quit-server emission.
 *
 * Exit codes submitted to e_dbus_server_quit() and returned by
 * e_dbus_server_run().
 *
 * Since: 3.6
 **/
typedef enum {
	E_DBUS_SERVER_EXIT_NONE,
	E_DBUS_SERVER_EXIT_NORMAL,
	E_DBUS_SERVER_EXIT_RELOAD
} EDBusServerExitCode;

/**
 * ESourcePermissionFlags:
 * @E_SOURCE_PERMISSION_NONE:
 *   The data source gets no initial permissions.
 * @E_SOURCE_PERMISSION_WRITABLE:
 *   The data source is initially writable.
 * @E_SOURCE_PERMISSION_REMOVABLE:
 *   The data source is initially removable.
 *
 * Initial permissions for a newly-loaded data source key file.
 *
 * Since: 3.6
 **/
typedef enum { /*< flags >*/
	E_SOURCE_PERMISSION_NONE = 0,
	E_SOURCE_PERMISSION_WRITABLE = 1 << 0,
	E_SOURCE_PERMISSION_REMOVABLE = 1 << 1
} ESourcePermissionFlags;

#endif /* E_BACKEND_ENUMS_H */
