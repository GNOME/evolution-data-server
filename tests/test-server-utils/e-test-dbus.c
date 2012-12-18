/* GIO testing utilities
 *
 * Copyright (C) 2008-2010 Red Hat, Inc.
 * Copyright (C) 2012 Collabora Ltd. <http://www.collabora.co.uk/>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General
 * Public License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place, Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Authors: David Zeuthen <davidz@redhat.com>
 *          Xavier Claessens <xavier.claessens@collabora.co.uk>
 *
 *
 * NOTE: This was directly copied from GIO library temporarily
 * while we depend on glib 2.32
 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#include <glib.h>
#include <glib/gstdio.h>
#include <gio/gio.h>

#include "e-test-dbus.h"

/* -------------------------------------------------------------------------- */
/* Utility: Wait until object has a single ref  */

typedef struct
{
  GMainLoop *loop;
  gboolean   timed_out;
} WeakNotifyData;

static gboolean
on_weak_notify_timeout (gpointer user_data)
{
  WeakNotifyData *data = user_data;
  data->timed_out = TRUE;
  g_main_loop_quit (data->loop);
  return FALSE;
}

static gboolean
unref_on_idle (gpointer object)
{
  g_object_unref (object);
  return FALSE;
}

static gboolean
_g_object_unref_and_wait_weak_notify (gpointer object)
{
  WeakNotifyData data;
  guint timeout_id;

  data.loop = g_main_loop_new (NULL, FALSE);
  data.timed_out = FALSE;

  g_object_weak_ref (object, (GWeakNotify) g_main_loop_quit, data.loop);

  /* Drop the ref in an idle callback, this is to make sure the mainloop
   * is already running when weak notify happens */
  g_idle_add (unref_on_idle, object);

  /* Make sure we don't block forever */
  timeout_id = g_timeout_add (30 * 1000, on_weak_notify_timeout, &data);

  g_main_loop_run (data.loop);

  g_source_remove (timeout_id);

  if (data.timed_out)
    {
      g_warning ("Weak notify timeout, object ref_count=%d\n",
          G_OBJECT (object)->ref_count);
    }

  return data.timed_out;
}

/* -------------------------------------------------------------------------- */
/* Utilities to cleanup the mess in the case unit test process crash */

#define ADD_PID_FORMAT "add pid %d\n"
#define REMOVE_PID_FORMAT "remove pid %d\n"

static void
watch_parent (gint fd)
{
  GIOChannel *channel;
  GPollFD fds[1];
  GArray *pids_to_kill;

  channel = g_io_channel_unix_new (fd);

  fds[0].fd = fd;
  fds[0].events = G_IO_HUP | G_IO_IN;
  fds[0].revents = 0;

  pids_to_kill = g_array_new (FALSE, FALSE, sizeof (guint));

  do
    {
      gint num_events;
      gchar *command = NULL;
      guint pid;
      guint n;
      GError *error = NULL;

      num_events = g_poll (fds, 1, -1);
      if (num_events == 0)
        continue;

      if (fds[0].revents == G_IO_HUP)
        {
          /* Parent quit, cleanup the mess and exit */
          for (n = 0; n < pids_to_kill->len; n++)
            {
              pid = g_array_index (pids_to_kill, guint, n);
              g_print ("cleaning up pid %d\n", pid);
              kill (pid, SIGTERM);
            }

          g_array_unref (pids_to_kill);
          g_io_channel_shutdown (channel, FALSE, &error);
          g_assert_no_error (error);
          g_io_channel_unref (channel);

          exit (0);
        }

      /* Read the command from the input */
      g_io_channel_read_line (channel, &command, NULL, NULL, &error);
      g_assert_no_error (error);

      /* Check for known commands */
      if (sscanf (command, ADD_PID_FORMAT, &pid) == 1)
        {
          g_array_append_val (pids_to_kill, pid);
        }
      else if (sscanf (command, REMOVE_PID_FORMAT, &pid) == 1)
        {
          for (n = 0; n < pids_to_kill->len; n++)
            {
              if (g_array_index (pids_to_kill, guint, n) == pid)
                {
                  g_array_remove_index (pids_to_kill, n);
                  pid = 0;
                  break;
                }
            }
          if (pid != 0)
            {
              g_warning ("unknown pid %d to remove", pid);
            }
        }
      else
        {
          g_warning ("unknown command from parent '%s'", command);
        }

      g_free (command);
    }
  while (TRUE);
}

static GIOChannel *
watcher_init (void)
{
  static gsize started = 0;
  static GIOChannel *channel = NULL;

  if (g_once_init_enter (&started))
    {
      gint pipe_fds[2];

      /* fork a child to clean up when we are killed */
      if (pipe (pipe_fds) != 0)
        {
          g_warning ("pipe() failed: %m");
          g_assert_not_reached ();
        }

      switch (fork ())
        {
        case -1:
          g_warning ("fork() failed: %m");
          g_assert_not_reached ();
          break;

        case 0:
          /* child */
          close (pipe_fds[1]);
          watch_parent (pipe_fds[0]);
          break;

        default:
          /* parent */
          close (pipe_fds[0]);
          channel = g_io_channel_unix_new (pipe_fds[1]);
        }

      g_once_init_leave (&started, 1);
    }

  return channel;
}

static void
watcher_send_command (const gchar *command)
{
  GIOChannel *channel;
  GError *error = NULL;

  channel = watcher_init ();

  g_io_channel_write_chars (channel, command, -1, NULL, &error);
  g_assert_no_error (error);

  g_io_channel_flush (channel, &error);
  g_assert_no_error (error);
}

/* This could be interesting to expose in public API */
static void
_g_test_watcher_add_pid (GPid pid)
{
  gchar *command;

  command = g_strdup_printf (ADD_PID_FORMAT, (guint) pid);
  watcher_send_command (command);
  g_free (command);
}

static void
_g_test_watcher_remove_pid (GPid pid)
{
  gchar *command;

  command = g_strdup_printf (REMOVE_PID_FORMAT, (guint) pid);
  watcher_send_command (command);
  g_free (command);
}

/* -------------------------------------------------------------------------- */
/* ETestDBus object implementation */


typedef struct _ETestDBusClass   ETestDBusClass;
typedef struct _ETestDBusPrivate ETestDBusPrivate;

/**
 * ETestDBus:
 *
 * The #ETestDBus structure contains only private data and
 * should only be accessed using the provided API.
 *
 * Since: 2.34
 */
struct _ETestDBus {
  GObject parent;

  ETestDBusPrivate *priv;
};

struct _ETestDBusClass {
  GObjectClass parent_class;
};

struct _ETestDBusPrivate
{
  ETestDBusFlags flags;
  GPtrArray *service_dirs;
  GPid bus_pid;
  gchar *bus_address;
  gboolean up;
};

enum
{
  PROP_0,
  PROP_FLAGS,
};

G_DEFINE_TYPE (ETestDBus, e_test_dbus, G_TYPE_OBJECT)

static void
e_test_dbus_init (ETestDBus *self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE ((self), G_TYPE_TEST_DBUS,
      ETestDBusPrivate);

  self->priv->service_dirs = g_ptr_array_new_with_free_func (g_free);
}

static void
e_test_dbus_dispose (GObject *object)
{
  ETestDBus *self = (ETestDBus *) object;

  if (self->priv->up)
    e_test_dbus_down (self);

  G_OBJECT_CLASS (e_test_dbus_parent_class)->dispose (object);
}

static void
e_test_dbus_finalize (GObject *object)
{
  ETestDBus *self = (ETestDBus *) object;

  g_ptr_array_unref (self->priv->service_dirs);
  g_free (self->priv->bus_address);

  G_OBJECT_CLASS (e_test_dbus_parent_class)->finalize (object);
}

static void
e_test_dbus_get_property (GObject *object,
    guint property_id,
    GValue *value,
    GParamSpec *pspec)
{
  ETestDBus *self = (ETestDBus *) object;

  switch (property_id)
    {
      case PROP_FLAGS:
        g_value_set_flags (value, e_test_dbus_get_flags (self));
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
e_test_dbus_set_property (GObject *object,
    guint property_id,
    const GValue *value,
    GParamSpec *pspec)
{
  ETestDBus *self = (ETestDBus *) object;

  switch (property_id)
    {
      case PROP_FLAGS:
        self->priv->flags = g_value_get_flags (value);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
e_test_dbus_class_init (ETestDBusClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = e_test_dbus_dispose;
  object_class->finalize = e_test_dbus_finalize;
  object_class->get_property = e_test_dbus_get_property;
  object_class->set_property = e_test_dbus_set_property;

  g_type_class_add_private (object_class, sizeof (ETestDBusPrivate));

  /**
   * ETestDBus:flags:
   *
   * #ETestDBusFlags specifying the behaviour of the dbus session
   *
   * Since: 2.34
   */
  g_object_class_install_property (object_class, PROP_FLAGS,
    g_param_spec_flags ("flags",
                        "dbus session flags",
                        "Flags specifying the behaviour of the dbus session",
                        G_TYPE_TEST_DBUS_FLAGS, E_TEST_DBUS_NONE,
                        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                        G_PARAM_STATIC_STRINGS));

}

static gchar *
write_config_file (ETestDBus *self)
{
  GString *contents;
  gint fd;
  guint i;
  GError *error = NULL;
  gchar *path = NULL;

  fd = g_file_open_tmp ("g-test-dbus-XXXXXX", &path, &error);
  g_assert_no_error (error);

  contents = g_string_new (NULL);
  g_string_append (contents,
      "<busconfig>\n"
      "  <type>session</type>\n"
#ifdef G_OS_WIN32
      "  <listen>nonce-tcp:</listen>\n"
#else
      "  <listen>unix:tmpdir=/tmp</listen>\n"
#endif
		   );

  for (i = 0; i < self->priv->service_dirs->len; i++)
    {
      const gchar *path = g_ptr_array_index (self->priv->service_dirs, i);

      g_string_append_printf (contents,
          "  <servicedir>%s</servicedir>\n", path);
    }

  g_string_append (contents,
      "  <policy context=\"default\">\n"
      "    <!-- Allow everything to be sent -->\n"
      "    <allow send_destination=\"*\" eavesdrop=\"true\"/>\n"
      "    <!-- Allow everything to be received -->\n"
      "    <allow eavesdrop=\"true\"/>\n"
      "    <!-- Allow anyone to own anything -->\n"
      "    <allow own=\"*\"/>\n"
      "  </policy>\n"
      "</busconfig>\n");

  g_file_set_contents (path, contents->str, contents->len, &error);
  g_assert_no_error (error);

  g_string_free (contents, TRUE);

  close (fd);

  return path;
}

static void
start_daemon (ETestDBus *self)
{
  gchar *argv[] = {"dbus-daemon", "--print-address", "--config-file=foo", NULL};
  gchar *config_path;
  gchar *config_arg;
  gint stdout_fd;
  GIOChannel *channel;
  gsize termpos;
  GError *error = NULL;

  if (g_getenv ("E_TEST_DBUS_DAEMON") != NULL)
    argv[0] = (gchar *)g_getenv ("E_TEST_DBUS_DAEMON");

  /* Write config file and set its path in argv */
  config_path = write_config_file (self);
  config_arg = g_strdup_printf ("--config-file=%s", config_path);
  argv[2] = config_arg;

  /* Spawn dbus-daemon */
  g_spawn_async_with_pipes (NULL,
                            argv,
                            NULL,
#ifdef G_OS_WIN32
                            /* We Need this to get the pid returned on win32 */
                            G_SPAWN_DO_NOT_REAP_CHILD |
#endif
                            G_SPAWN_SEARCH_PATH,
                            NULL,
                            NULL,
                            &self->priv->bus_pid,
                            NULL,
                            &stdout_fd,
                            NULL,
                            &error);
  g_assert_no_error (error);

  _g_test_watcher_add_pid (self->priv->bus_pid);

  /* Read bus address from daemon' stdout */
  channel = g_io_channel_unix_new (stdout_fd);
  g_io_channel_read_line (channel, &self->priv->bus_address, NULL,
      &termpos, &error);
  g_assert_no_error (error);
  self->priv->bus_address[termpos] = '\0';

  /* start dbus-monitor */
  if (g_getenv ("G_DBUS_MONITOR") != NULL)
    {
      gchar *command;

      command = g_strdup_printf ("dbus-monitor --address %s",
          self->priv->bus_address);
      g_spawn_command_line_async (command, NULL);
      g_free (command);

      g_usleep (500 * 1000);
    }

  /* Cleanup */
  g_io_channel_shutdown (channel, FALSE, &error);
  g_assert_no_error (error);
  g_io_channel_unref (channel);

  /* Don't use g_file_delete since it calls into gvfs */
  if (g_unlink (config_path) != 0)
    g_assert_not_reached ();

  g_free (config_path);
  g_free (config_arg);
}

static void
stop_daemon (ETestDBus *self)
{
#ifdef G_OS_WIN32
  if (!TerminateProcess (self->priv->bus_pid, 0))
    g_warning ("Can't terminate process: %s", g_win32_error_message (GetLastError()));
#else
  kill (self->priv->bus_pid, SIGTERM);
#endif
  _g_test_watcher_remove_pid (self->priv->bus_pid);
  g_spawn_close_pid (self->priv->bus_pid);
  self->priv->bus_pid = 0;

  g_free (self->priv->bus_address);
  self->priv->bus_address = NULL;
}

/**
 * e_test_dbus_new:
 * @flags: a #ETestDBusFlags
 *
 * Create a new #ETestDBus object.
 *
 * Returns: (transfer full): a new #ETestDBus.
 */
ETestDBus *
e_test_dbus_new (ETestDBusFlags flags)
{
  return g_object_new (G_TYPE_TEST_DBUS,
      "flags", flags,
      NULL);
}

/**
 * e_test_dbus_get_flags:
 * @self: a #ETestDBus
 *
 * Gets the flags of the #ETestDBus object.
 *
 * Returns: the value of #ETestDBus:flags property
 */
ETestDBusFlags
e_test_dbus_get_flags (ETestDBus *self)
{
  g_return_val_if_fail (G_IS_TEST_DBUS (self), E_TEST_DBUS_NONE);

  return self->priv->flags;
}

/**
 * e_test_dbus_get_bus_address:
 * @self: a #ETestDBus
 *
 * Get the address on which dbus-daemon is running. if e_test_dbus_up() has not
 * been called yet, %NULL is returned. This can be used with
 * g_dbus_connection_new_for_address()
 *
 * Returns: the address of the bus, or %NULL.
 */
const gchar *
e_test_dbus_get_bus_address (ETestDBus *self)
{
  g_return_val_if_fail (G_IS_TEST_DBUS (self), NULL);

  return self->priv->bus_address;
}

/**
 * e_test_dbus_add_service_dir:
 * @self: a #ETestDBus
 * @path: path to a directory containing .service files
 *
 * Add a path where dbus-daemon will lookup for .services files. This can't be
 * called after e_test_dbus_up().
 */
void
e_test_dbus_add_service_dir (ETestDBus *self,
    const gchar *path)
{
  g_return_if_fail (G_IS_TEST_DBUS (self));
  g_return_if_fail (self->priv->bus_address == NULL);

  g_ptr_array_add (self->priv->service_dirs, g_strdup (path));
}

/**
 * e_test_dbus_up:
 * @self: a #ETestDBus
 *
 * Start a dbus-daemon instance and set DBUS_SESSION_BUS_ADDRESS. After this
 * call, it is safe for unit tests to start sending messages on the session bus.
 *
 * If this function is called from setup callback of g_test_add(),
 * e_test_dbus_down() must be called in its teardown callback.
 *
 * If this function is called from unit test's main(), then e_test_dbus_down()
 * must be called after g_test_run().
 */
void
e_test_dbus_up (ETestDBus *self)
{
  g_return_if_fail (G_IS_TEST_DBUS (self));
  g_return_if_fail (self->priv->bus_address == NULL);
  g_return_if_fail (!self->priv->up);

  start_daemon (self);

  g_setenv ("DBUS_SESSION_BUS_ADDRESS", self->priv->bus_address, TRUE);
  self->priv->up = TRUE;
}


/**
 * e_test_dbus_stop:
 * @self: a #ETestDBus
 *
 * Stop the session bus started by e_test_dbus_up().
 *
 * Unlike e_test_dbus_down(), this won't verify the #GDBusConnection
 * singleton returned by g_bus_get() or g_bus_get_sync() is destroyed. Unit
 * tests wanting to verify behaviour after the session bus has been stopped
 * can use this function but should still call e_test_dbus_down() when done.
 */
void
e_test_dbus_stop (ETestDBus *self)
{
  g_return_if_fail (G_IS_TEST_DBUS (self));
  g_return_if_fail (self->priv->bus_address != NULL);

  stop_daemon (self);
}

/**
 * e_test_dbus_down:
 * @self: a #ETestDBus
 *
 * Stop the session bus started by e_test_dbus_up().
 *
 * This will wait for the singleton returned by g_bus_get() or g_bus_get_sync()
 * is destroyed. This is done to ensure that the next unit test won't get a
 * leaked singleton from this test.
 */
void
e_test_dbus_down (ETestDBus *self)
{
  GDBusConnection *connection = NULL;

  g_return_if_fail (G_IS_TEST_DBUS (self));
  g_return_if_fail (self->priv->up);

  /* connection = _g_bus_get_singleton_if_exists (G_BUS_TYPE_SESSION); */
  /* if (connection != NULL) */
  /*   g_dbus_connection_set_exit_on_close (connection, FALSE); */

  if (self->priv->bus_address != NULL)
    stop_daemon (self);

  if (connection != NULL)
    _g_object_unref_and_wait_weak_notify (connection);

  g_unsetenv ("DBUS_SESSION_BUS_ADDRESS");
  self->priv->up = FALSE;
}

/**
 * e_test_dbus_unset:
 *
 * Unset DISPLAY and DBUS_SESSION_BUS_ADDRESS env variables to ensure the test
 * won't use user's session bus.
 *
 * This is useful for unit tests that want to verify behaviour when no session
 * bus is running. It is not necessary to call this if unit test already calls
 * e_test_dbus_up() before acquiring the session bus.
 */
void
e_test_dbus_unset (void)
{
  g_unsetenv ("DISPLAY");
  g_unsetenv ("DBUS_SESSION_BUS_ADDRESS");
}
