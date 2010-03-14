/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include "e-flag.h"

struct _EFlag {
	GCond *cond;
	GMutex *mutex;
	gboolean is_set;
};

/**
 * e_flag_new:
 *
 * Creates a new #EFlag object.  It is initially unset.
 *
 * Returns: a new #EFlag
 *
 * Since: 1.12
 **/
EFlag *
e_flag_new (void)
{
	EFlag *flag;

	flag = g_slice_new (EFlag);
	flag->cond = g_cond_new ();
	flag->mutex = g_mutex_new ();
	flag->is_set = FALSE;

	return flag;
}

/**
 * e_flag_is_set:
 * @flag: an #EFlag
 *
 * Returns the state of @flag.
 *
 * Returns: %TRUE if @flag is set
 *
 * Since: 1.12
 **/
gboolean
e_flag_is_set (EFlag *flag)
{
	gboolean is_set;

	g_return_val_if_fail (flag != NULL, FALSE);

	g_mutex_lock (flag->mutex);
	is_set = flag->is_set;
	g_mutex_unlock (flag->mutex);

	return is_set;
}

/**
 * e_flag_set:
 * @flag: an #EFlag
 *
 * Sets @flag.  All threads waiting on @flag are woken up.  Threads that
 * call e_flag_wait() or e_flag_timed_wait() once @flag is set will not
 * block at all.
 *
 * Since: 1.12
 **/
void
e_flag_set (EFlag *flag)
{
	g_return_if_fail (flag != NULL);

	g_mutex_lock (flag->mutex);
	flag->is_set = TRUE;
	g_cond_broadcast (flag->cond);
	g_mutex_unlock (flag->mutex);
}

/**
 * e_flag_clear:
 * @flag: an #EFlag
 *
 * Unsets @flag.  Subsequent calls to e_flag_wait() or e_flag_timed_wait()
 * will block until @flag is set.
 *
 * Since: 1.12
 **/
void
e_flag_clear (EFlag *flag)
{
	g_return_if_fail (flag != NULL);

	g_mutex_lock (flag->mutex);
	flag->is_set = FALSE;
	g_mutex_unlock (flag->mutex);
}

/**
 * e_flag_wait:
 * @flag: an #EFlag
 *
 * Blocks until @flag is set.  If @flag is already set, the function returns
 * immediately.
 *
 * Since: 1.12
 **/
void
e_flag_wait (EFlag *flag)
{
	g_return_if_fail (flag != NULL);

	g_mutex_lock (flag->mutex);
	while (!flag->is_set)
		g_cond_wait (flag->cond, flag->mutex);
	g_mutex_unlock (flag->mutex);
}

/**
 * e_flag_timed_wait:
 * @flag: an #EFlag
 * @abs_time: a #GTimeVal, determining the final time
 *
 * Blocks until @flag is set, or until the time specified by @abs_time.
 * If @flag is already set, the function returns immediately.  The return
 * value indicates the state of @flag after waiting.
 *
 * If @abs_time is %NULL, e_flag_timed_wait() acts like e_flag_wait().
 *
 * To easily calculate @abs_time, a combination of g_get_current_time() and
 * g_time_val_add() can be used.
 *
 * Returns: %TRUE if @flag is now set
 *
 * Since: 1.12
 **/
gboolean
e_flag_timed_wait (EFlag *flag, GTimeVal *abs_time)
{
	gboolean is_set;

	g_return_val_if_fail (flag != NULL, FALSE);

	g_mutex_lock (flag->mutex);
	while (!flag->is_set)
		if (!g_cond_timed_wait (flag->cond, flag->mutex, abs_time))
			break;
	is_set = flag->is_set;
	g_mutex_unlock (flag->mutex);

	return is_set;
}

/**
 * e_flag_free:
 * @flag: an #EFlag
 *
 * Destroys @flag.
 *
 * Since: 1.12
 **/
void
e_flag_free (EFlag *flag)
{
	g_return_if_fail (flag != NULL);

	g_cond_free (flag->cond);
	g_mutex_free (flag->mutex);
	g_slice_free (EFlag, flag);
}
