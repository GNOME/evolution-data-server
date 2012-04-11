/*
 * e-backend-enums.h
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with the program; if not, see <http://www.gnu.org/licenses/>
 *
 */

#ifndef E_BACKEND_ENUMS_H
#define E_BACKEND_ENUMS_H

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

#endif /* E_BACKEND_ENUMS_H */

