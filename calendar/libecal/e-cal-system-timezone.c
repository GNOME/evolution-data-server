/* Evolution calendar system timezone functions
 * Based on gnome-panel's clock-applet system-timezone.c file.
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <gio/gio.h>
#include <string.h>
#include "e-cal-system-timezone.h"

#ifdef HAVE_SOLARIS
#define SYSTEM_ZONEINFODIR "/usr/share/lib/zoneinfo/tab"
#else
#define SYSTEM_ZONEINFODIR "/usr/share/zoneinfo"
#endif

#define ETC_TIMEZONE        "/etc/timezone"
#define ETC_TIMEZONE_MAJ    "/etc/TIMEZONE"
#define ETC_RC_CONF         "/etc/rc.conf"
#define ETC_SYSCONFIG_CLOCK "/etc/sysconfig/clock"
#define ETC_CONF_D_CLOCK    "/etc/conf.d/clock"
#define ETC_LOCALTIME       "/etc/localtime"

#define TZ_MAGIC "TZif"

static gchar *
system_timezone_strip_path_if_valid (const gchar *filename)
{
	gint skip;

	if (!filename || !g_str_has_prefix (filename, SYSTEM_ZONEINFODIR"/"))
		return NULL;

	/* Timezone data files also live under posix/ and right/ for some
	 * reason.
	 * FIXME: make sure accepting those files is valid. I think "posix" is
	 * okay, not sure about "right" */
	if (g_str_has_prefix (filename, SYSTEM_ZONEINFODIR"/posix/"))
		skip = strlen (SYSTEM_ZONEINFODIR"/posix/");
	else if (g_str_has_prefix (filename, SYSTEM_ZONEINFODIR"/right/"))
		skip = strlen (SYSTEM_ZONEINFODIR"/right/");
	else
		skip = strlen (SYSTEM_ZONEINFODIR"/");

	return g_strdup (filename + skip);
}

/* Read the soft symlink from /etc/localtime */
static gchar *
system_timezone_read_etc_localtime_softlink (void)
{
	gchar *file;
	gchar *tz;

	if (!g_file_test (ETC_LOCALTIME, G_FILE_TEST_IS_SYMLINK))
		return NULL;

	file = g_file_read_link (ETC_LOCALTIME, NULL);
	tz = system_timezone_strip_path_if_valid (file);
	g_free (file);

	return tz;
}

static gchar *
system_timezone_read_etc_timezone (void)
{
        FILE    *etc_timezone;
        GString *reading;
        gint      c;

        etc_timezone = g_fopen (ETC_TIMEZONE, "r");
        if (!etc_timezone)
                return NULL;

        reading = g_string_new ("");

        c = fgetc (etc_timezone);
        /* only get the first line, we'll validate the value later */
        while (c != EOF && !g_ascii_isspace (c)) {
                reading = g_string_append_c (reading, c);
                c = fgetc (etc_timezone);
        }

        fclose (etc_timezone);

        if (reading->str && reading->str[0] != '\0')
                return g_string_free (reading, FALSE);
        else
                g_string_free (reading, TRUE);

        return NULL;
}

/* Read a file that looks like a key-file (but there's no need for groups)
 * and get the last value for a specific key */
static gchar *
system_timezone_read_key_file (const gchar *filename,
                               const gchar *key)
{
        GIOChannel *channel;
        gchar       *key_eq;
        gchar       *line;
        gchar       *retval;

        if (!g_file_test (filename, G_FILE_TEST_IS_REGULAR))
                return NULL;

        channel = g_io_channel_new_file (filename, "r", NULL);
        if (!channel)
                return NULL;

        key_eq = g_strdup_printf ("%s=", key);
        retval = NULL;

        while (g_io_channel_read_line (channel, &line, NULL,
                                       NULL, NULL) == G_IO_STATUS_NORMAL) {
                if (g_str_has_prefix (line, key_eq)) {
                        gchar *value;
                        gint   len;

                        value = line + strlen (key_eq);
                        g_strstrip (value);

                        len = strlen (value);

                        if (value[0] == '\"') {
                                if (value[len - 1] == '\"') {
                                        if (retval)
                                                g_free (retval);

                                        retval = g_strndup (value + 1,
                                                            len - 2);
                                }
                        } else {
                                if (retval)
                                        g_free (retval);

                                retval = g_strdup (line + strlen (key_eq));
                        }

                        g_strstrip (retval);
                }

                g_free (line);
        }

        g_free (key_eq);
        g_io_channel_unref (channel);

        return retval;
}

/* This works for Fedora and Mandriva */
static gchar *
system_timezone_read_etc_sysconfig_clock (void)
{
        return system_timezone_read_key_file (ETC_SYSCONFIG_CLOCK,
                                              "ZONE");
}

/* This works for openSUSE */
static gchar *
system_timezone_read_etc_sysconfig_clock_alt (void)
{
        return system_timezone_read_key_file (ETC_SYSCONFIG_CLOCK,
                                              "TIMEZONE");
}

/* This works for Solaris/OpenSolaris */
static gchar *
system_timezone_read_etc_TIMEZONE (void)
{
        return system_timezone_read_key_file (ETC_TIMEZONE_MAJ,
                                              "TZ");
}

/* This works for Arch Linux */
static gchar *
system_timezone_read_etc_rc_conf (void)
{
        return system_timezone_read_key_file (ETC_RC_CONF,
                                              "TIMEZONE");
}

/* This works for old Gentoo */
static gchar *
system_timezone_read_etc_conf_d_clock (void)
{
        return system_timezone_read_key_file (ETC_CONF_D_CLOCK,
                                              "TIMEZONE");
}

typedef gboolean (*CompareFiles) (struct stat *a_stat,
				  struct stat *b_stat,
				  const gchar  *a_content,
				  gsize	a_content_len,
				  const gchar  *b_filename);

static gchar *
recursive_compare (struct stat  *localtime_stat,
		   const gchar   *localtime_content,
		   gsize	 localtime_content_len,
		   const gchar	*file,
		   CompareFiles  compare_func)
{
	struct stat file_stat;

	if (g_stat (file, &file_stat) != 0)
		return NULL;

	if (S_ISREG (file_stat.st_mode)) {
		if (compare_func (localtime_stat,
				  &file_stat,
				  localtime_content,
				  localtime_content_len,
				  file))
			return system_timezone_strip_path_if_valid (file);
		else
			return NULL;
	} else if (S_ISDIR (file_stat.st_mode)) {
		GDir       *dir = NULL;
		gchar       *ret = NULL;
		const gchar *subfile = NULL;
		gchar       *subpath = NULL;

		dir = g_dir_open (file, 0, NULL);
		if (dir == NULL)
			return NULL;

		while ((subfile = g_dir_read_name (dir)) != NULL) {
			subpath = g_build_filename (file, subfile, NULL);

			ret = recursive_compare (localtime_stat,
						 localtime_content,
						 localtime_content_len,
						 subpath,
						 compare_func);

			g_free (subpath);

			if (ret != NULL)
				break;
		}

		g_dir_close (dir);

		return ret;
	}

	return NULL;
}

static gboolean
files_are_identical_inode (struct stat *a_stat,
			   struct stat *b_stat,
			   const gchar  *a_content,
			   gsize	a_content_len,
			   const gchar  *b_filename)
{
	gboolean res = a_stat->st_ino == b_stat->st_ino;

	if (res) {
		const gchar *filename;

		filename = strrchr (b_filename, '/');
		if (filename)
			filename++;
		else
			filename = b_filename;

		/* There is a 'localtime' soft link to /etc/localtime in the zoneinfo
		   directory on Slackware, thus rather skip this file. */
		res = !g_str_equal (filename, "localtime");
	}

	return res;
}

/* Determine if /etc/localtime is a hard link to some file, by looking at
 * the inodes */
static gchar *
system_timezone_read_etc_localtime_hardlink (void)
{
	struct stat stat_localtime;

	if (g_stat (ETC_LOCALTIME, &stat_localtime) != 0)
		return NULL;

	if (!S_ISREG (stat_localtime.st_mode))
		return NULL;

	return recursive_compare (&stat_localtime,
				  NULL,
				  0,
				  SYSTEM_ZONEINFODIR,
				  files_are_identical_inode);
}

static gboolean
files_are_identical_content (struct stat *a_stat,
			     struct stat *b_stat,
			     const gchar  *a_content,
			     gsize        a_content_len,
			     const gchar  *b_filename)
{
	gchar  *b_content = NULL;
	gsize  b_content_len = -1;
	gint    cmp;

	if (a_stat->st_size != b_stat->st_size)
		return FALSE;

	if (!g_file_get_contents (b_filename,
				 &b_content, &b_content_len, NULL))
		return FALSE;

	if (a_content_len != b_content_len) {
		g_free (b_content);
		return FALSE;
	}

	cmp = memcmp (a_content, b_content, a_content_len);
	g_free (b_content);

	return (cmp == 0);
}

/* Determine if /etc/localtime is a copy of a timezone file */
static gchar *
system_timezone_read_etc_localtime_content (void)
{
	struct stat  stat_localtime;
	gchar	*localtime_content = NULL;
	gsize	localtime_content_len = -1;
	gchar	*retval;

	if (g_stat (ETC_LOCALTIME, &stat_localtime) != 0)
		return NULL;

	if (!S_ISREG (stat_localtime.st_mode))
		return NULL;

	if (!g_file_get_contents (ETC_LOCALTIME,
				  &localtime_content,
				  &localtime_content_len,
				  NULL))
		return NULL;

	retval = recursive_compare (&stat_localtime,
				   localtime_content,
				   localtime_content_len,
				   SYSTEM_ZONEINFODIR,
				   files_are_identical_content);

	g_free (localtime_content);

	return retval;
}

typedef gchar * (*GetSystemTimezone) (void);
/* The order of the functions here define the priority of the methods used
 * to find the timezone. First method has higher priority. */
static GetSystemTimezone get_system_timezone_methods[] = {
	/* cheap and "more correct" than data from a config file */
	system_timezone_read_etc_localtime_softlink,
	/* reading various config files */
	system_timezone_read_etc_timezone,
	system_timezone_read_etc_sysconfig_clock,
	system_timezone_read_etc_sysconfig_clock_alt,
	system_timezone_read_etc_TIMEZONE,
	system_timezone_read_etc_rc_conf,
	/* reading deprecated config files */
	system_timezone_read_etc_conf_d_clock,
	/* reading /etc/timezone directly. Expensive since we have to stat
	 * many files */
	system_timezone_read_etc_localtime_hardlink,
	system_timezone_read_etc_localtime_content,
	NULL
};

static gboolean
system_timezone_is_valid (const gchar *tz)
{
	const gchar *c;

	if (!tz)
		return FALSE;

	for (c = tz; *c != '\0'; c++) {
		if (!(g_ascii_isalnum (*c) ||
		    *c == '/' || *c == '-' || *c == '_'))
			return FALSE;
	}

	return TRUE;
}

static gchar *
system_timezone_find (void)
{
	gchar *tz;
	gint   i;

	for (i = 0; get_system_timezone_methods[i] != NULL; i++) {
		tz = get_system_timezone_methods[i] ();

		if (system_timezone_is_valid (tz))
			return tz;

		g_free (tz);
	}

	return NULL;
}

/**
 * e_cal_system_timezone_get_location:
 *
 * Returns system timezone location string, NULL on an error.
 * Returned pointer should be freed with g_free().
 *
 * Since: 2.28
 **/
gchar *
e_cal_system_timezone_get_location (void)
{
	return system_timezone_find ();
}
