/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-win32.c : Win32-specific bits */

/*
 * Authors: Tor Lillqvist <tml@novell.com>
 *
 * Copyright 2005 Novell, Inc. (www.novell.com)
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */

#include <windows.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/stat.h>

#include <io.h>

#include <glib.h>
#include <glib/gstdio.h>

#include <libedataserver/e-util.h>

#include "camel.h"

G_LOCK_DEFINE_STATIC (mutex);

/* localedir uses system codepage as it is passed to the non-UTF8ified
 * gettext library
 */
static const char *localedir = NULL;

/* The others are in UTF-8 */
static const char *libexecdir;
static const char *providerdir;

static void
setup (void)
{
        G_LOCK (mutex);
        if (localedir != NULL) {
                G_UNLOCK (mutex);
                return;
        }

        localedir = e_util_replace_prefix (e_util_get_cp_prefix (), EVOLUTION_LOCALEDIR);

	libexecdir = e_util_replace_prefix (e_util_get_prefix (), CAMEL_LIBEXECDIR);
	providerdir = e_util_replace_prefix (e_util_get_prefix (), CAMEL_PROVIDERDIR);

	G_UNLOCK (mutex);
}

#include "camel-private.h"	/* For prototypes */

#define GETTER(varbl)				\
const char *					\
_camel_get_##varbl (void)			\
{						\
        setup ();				\
        return varbl;				\
}

GETTER(localedir)
GETTER(libexecdir)
GETTER(providerdir)

int
fsync (int fd)
{
	int handle;
	struct stat st;

	handle = _get_osfhandle (fd);
	if (handle == -1)
		return -1;

	fstat (fd, &st);

	/* FlushFileBuffers() fails if called on a handle to the
	 * console output. As we cannot know whether fd refers to the
	 * console output or not, punt, and call FlushFileBuffers()
	 * only for regular files and pipes.
	 */
	if (!(S_ISREG (st.st_mode) || S_ISFIFO (st.st_mode)))
		return 0;

	if (FlushFileBuffers ((HANDLE) handle))
		return 0;

	errno = EIO;
	return -1;
}

char *
realpath(const char *name,
	 char       *resolved)
{
  /* Lifted from glibc and modified as necessary */

  char *dest;
  const char *past_root;
  const char *start, *end, *resolved_limit;

  if (name == NULL || resolved == NULL)
    {
      /* As per Single Unix Specification V2 we must return an error if
	 either parameter is a null pointer.  */
      errno = EINVAL;
      return NULL;
    }

  if (name[0] == '\0')
    {
      /* As per Single Unix Specification V2 we must return an error if
	 the name argument points to an empty string.  */
      errno = ENOENT;
      return NULL;
    }

  resolved_limit = resolved + PATH_MAX;

  if (!g_path_is_absolute (name))
    {
      char *cwd = g_get_current_dir ();

      if (strlen (cwd) > PATH_MAX)
	{
	  g_free (cwd);
	  resolved[0] = '\0';
	  errno = ENAMETOOLONG;
	  goto error;
	}
      strcpy (resolved, cwd);
      g_free (cwd);
      past_root = g_path_skip_root (resolved);
      dest = strchr (resolved, '\0');
      start = name;
    }
  else
    {
      const char *n = name;
      start = g_path_skip_root (name);

      dest = resolved;
      while (dest != resolved_limit && n != start)
	{
	  if (G_IS_DIR_SEPARATOR (*n))
	    {
	      *dest++ = G_DIR_SEPARATOR;
	      n++;
	    }
	  else
	    *dest++ = *n++;
	}
      if (dest == resolved_limit)
	{
	  resolved[0] = '\0';
	  errno = ENAMETOOLONG;
	  goto error;
	}
      past_root = dest;
    }

  for (end = name; *start; start = end)
    {
      struct stat st;

      /* Skip sequence of multiple path-separators.  */
      while (G_IS_DIR_SEPARATOR (*start))
	++start;

      /* Find end of path component.  */
      for (end = start; *end && !G_IS_DIR_SEPARATOR (*end); ++end)
	/* Nothing.  */;

      if (end - start == 0)
	break;
      else if (end - start == 1 && start[0] == '.')
	/* nothing */;
      else if (end - start == 2 && start[0] == '.' && start[1] == '.')
	{
	  /* Back up to previous component, ignore if at root already.  */
	  if (dest > past_root)
	    {
	      --dest;
	      while (!G_IS_DIR_SEPARATOR ((dest)[-1]))
		--dest;
	    }
	}
      else
	{
	  if (!G_IS_DIR_SEPARATOR (dest[-1]))
	    *dest++ = G_DIR_SEPARATOR;

	  if (dest + (end - start) >= resolved_limit)
	    {
	      errno = ENAMETOOLONG;
	      if (dest > past_root)
		dest--;
	      *dest = '\0';
	      goto error;
	    }

	  memcpy (dest, start, end - start);
	  dest += end - start;
	  *dest = '\0';

	  if (g_stat (resolved, &st) < 0)
	    goto error;
	}
    }
  if (dest > past_root && G_IS_DIR_SEPARATOR (dest[-1]))
    --dest;
  *dest = '\0';

  return resolved;

error:
  return NULL;
}

