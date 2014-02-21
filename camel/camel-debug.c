/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- *
 *
 * Author: Michael Zucchi <notzed@ximian.com>
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This library is free software you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef HAVE_BACKTRACE_SYMBOLS
#include <execinfo.h>
#ifdef HAVE_ELFUTILS_LIBDWFL
#include <elfutils/libdwfl.h>
#include <errno.h>
#include <unistd.h>
#endif
#endif

#include "camel-debug.h"

gint camel_verbose_debug;

static GHashTable *debug_table = NULL;

/**
 * camel_debug_init:
 * @void:
 *
 * Init camel debug.
 *
 * CAMEL_DEBUG is set to a comma separated list of modules to debug.
 * The modules can contain module-specific specifiers after a ':', or
 * just act as a wildcard for the module or even specifier.  e.g. 'imap'
 * for imap debug, or 'imap:folder' for imap folder debug.  Additionaly,
 * ':folder' can be used for a wildcard for any folder operations.
 **/
void
camel_debug_init (void)
{
	gchar *d;

	d = g_strdup (getenv ("CAMEL_DEBUG"));
	if (d) {
		gchar *p;

		debug_table = g_hash_table_new (g_str_hash, g_str_equal);
		p = d;
		while (*p) {
			while (*p && *p != ',')
				p++;
			if (*p)
				*p++ = 0;
			g_hash_table_insert (debug_table, d, d);
			d = p;
		}

		if (g_hash_table_lookup (debug_table, "all"))
			camel_verbose_debug = 1;
	}
}

/**
 * camel_debug:
 * @mode:
 *
 * Check to see if a debug mode is activated.  @mode takes one of two forms,
 * a fully qualified 'module:target', or a wildcard 'module' name.  It
 * returns a boolean to indicate if the module or module and target is
 * currently activated for debug output.
 *
 * Returns:
 **/
gboolean camel_debug (const gchar *mode)
{
	if (camel_verbose_debug)
		return TRUE;

	if (debug_table) {
		gchar *colon;
		gchar *fallback;
		gsize fallback_len;

		if (g_hash_table_lookup (debug_table, mode))
			return TRUE;

		/* Check for fully qualified debug */
		colon = strchr (mode, ':');
		if (colon) {
			fallback_len = strlen (mode) + 1;
			fallback = g_alloca (fallback_len);
			g_strlcpy (fallback, mode, fallback_len);
			colon = (colon - mode) + fallback;
			/* Now check 'module[:*]' */
			*colon = 0;
			if (g_hash_table_lookup (debug_table, fallback))
				return TRUE;
			/* Now check ':subsystem' */
			*colon = ':';
			if (g_hash_table_lookup (debug_table, colon))
				return TRUE;
		}
	}

	return FALSE;
}

static GMutex debug_lock;
/**
 * camel_debug_start:
 * @mode:
 *
 * Start debug output for a given mode, used to make sure debug output
 * is output atomically and not interspersed with unrelated stuff.
 *
 * Returns: Returns true if mode is set, and in which case, you must
 * call debug_end when finished any screen output.
 **/
gboolean
camel_debug_start (const gchar *mode)
{
	if (camel_debug (mode)) {
		g_mutex_lock (&debug_lock);
		printf ("Thread %p >\n", g_thread_self ());
		return TRUE;
	}

	return FALSE;
}

/**
 * camel_debug_end:
 *
 * Call this when you're done with your debug output.  If and only if
 * you called camel_debug_start, and if it returns TRUE.
 **/
void
camel_debug_end (void)
{
	printf ("< %p >\n", g_thread_self ());
	g_mutex_unlock (&debug_lock);
}

#if 0
#include <sys/debugreg.h>

static unsigned
i386_length_and_rw_bits (gint len,
                         enum target_hw_bp_type type)
{
  unsigned rw;

  switch (type)
    {
      case hw_execute:
	rw = DR_RW_EXECUTE;
	break;
      case hw_write:
	rw = DR_RW_WRITE;
	break;
      case hw_read:      /* x86 doesn't support data-read watchpoints */
      case hw_access:
	rw = DR_RW_READ;
	break;
#if 0
      case hw_io_access: /* not yet supported */
	rw = DR_RW_IORW;
	break;
#endif
      default:
	internal_error (__FILE__, __LINE__, "Invalid hw breakpoint type %d in i386_length_and_rw_bits.\n", (gint) type);
    }

  switch (len)
    {
      case 1:
	return (DR_LEN_1 | rw);
      case 2:
	return (DR_LEN_2 | rw);
      case 4:
	return (DR_LEN_4 | rw);
      case 8:
	if (TARGET_HAS_DR_LEN_8)
	  return (DR_LEN_8 | rw);
      default:
	internal_error (__FILE__, __LINE__, "Invalid hw breakpoint length %d in i386_length_and_rw_bits.\n", len);
    }
}

#define I386_DR_SET_RW_LEN(i,rwlen) \
  do { \
    dr_control_mirror &= ~(0x0f << (DR_CONTROL_SHIFT + DR_CONTROL_SIZE * (i))); \
    dr_control_mirror |= ((rwlen) << (DR_CONTROL_SHIFT + DR_CONTROL_SIZE * (i))); \
  } while (0)

#define I386_DR_LOCAL_ENABLE(i) \
  dr_control_mirror |= (1 << (DR_LOCAL_ENABLE_SHIFT + DR_ENABLE_SIZE * (i)))

#define set_dr(regnum, val) \
		__asm__("movl %0,%%db" #regnum \
			: /* no output */ \
			:"r" (val))

#define get_dr(regnum, val) \
		__asm__("movl %%db" #regnum ", %0" \
			:"=r" (val))

/* fine idea, but it doesn't work, crashes in get_dr :-/ */
void
camel_debug_hwatch (gint wp,
                    gpointer addr)
{
     guint32 control, rw;

     g_assert (wp <= DR_LASTADDR);
     g_assert (sizeof (addr) == 4);

     get_dr (7, control);
     /* set watch mode + size */
     rw = DR_RW_WRITE | DR_LEN_4;
     control &= ~(((1 << DR_CONTROL_SIZE) - 1) << (DR_CONTROL_SHIFT + DR_CONTROL_SIZE * wp));
     control |= rw << (DR_CONTROL_SHIFT + DR_CONTROL_SIZE * wp);
     /* set watch enable */
     control |= ( 1<< (DR_LOCAL_ENABLE_SHIFT + DR_ENABLE_SIZE * wp));
     control |= DR_LOCAL_SLOWDOWN;
     control &= ~DR_CONTROL_RESERVED;

     switch (wp) {
     case 0:
	     set_dr (0, addr);
	     break;
     case 1:
	     set_dr (1, addr);
	     break;
     case 2:
	     set_dr (2, addr);
	     break;
     case 3:
	     set_dr (3, addr);
	     break;
     }
     set_dr (7, control);
}

#endif

G_LOCK_DEFINE_STATIC (ptr_tracker);
static GHashTable *ptr_tracker = NULL;

struct pt_data {
	gpointer ptr;
	gchar *info;
	GString *backtrace;
};

static void
free_pt_data (gpointer ptr)
{
	struct pt_data *ptd = ptr;

	if (!ptd)
		return;

	g_free (ptd->info);
	if (ptd->backtrace)
		g_string_free (ptd->backtrace, TRUE);
	g_free (ptd);
}

static void
dump_left_ptrs_cb (gpointer key,
                   gpointer value,
                   gpointer user_data)
{
	guint *left = user_data;
	struct pt_data *ptd = value;
	gboolean have_info = ptd && ptd->info;
	gboolean have_bt = ptd && ptd->backtrace && ptd->backtrace->str && *ptd->backtrace->str;

	*left = (*left) - 1;
	g_print ("      %p %s%s%s%s%s%s\n", key, have_info ? "(" : "", have_info ? ptd->info : "", have_info ? ")" : "", have_bt ? "\n" : "", have_bt ? ptd->backtrace->str : "", have_bt && *left > 0 ? "\n" : "");
}

#ifdef HAVE_BACKTRACE_SYMBOLS
static guint
by_backtrace_hash (gconstpointer ptr)
{
	const struct pt_data *ptd = ptr;

	if (!ptd || !ptd->backtrace)
		return 0;

	return g_str_hash (ptd->backtrace->str);
}

static gboolean
by_backtrace_equal (gconstpointer ptr1,
                    gconstpointer ptr2)
{
	const struct pt_data *ptd1 = ptr1, *ptd2 = ptr2;

	if ((!ptd1 || !ptd1->backtrace) && (!ptd2 || !ptd2->backtrace))
		return TRUE;

	return ptd1 && ptd1->backtrace && ptd2 && ptd2->backtrace && g_str_equal (ptd1->backtrace->str, ptd2->backtrace->str);
}

static void
dump_by_backtrace_cb (gpointer key,
                      gpointer value,
                      gpointer user_data)
{
	guint *left = user_data;
	struct pt_data *ptd = key;
	guint count = GPOINTER_TO_UINT (value);

	if (count == 1) {
		dump_left_ptrs_cb (ptd->ptr, ptd, left);
	} else {
		gboolean have_info = ptd && ptd->info;
		gboolean have_bt = ptd && ptd->backtrace && ptd->backtrace->str && *ptd->backtrace->str;

		*left = (*left) - 1;

		g_print ("      %d x %s%s%s%s%s%s\n", count, have_info ? "(" : "", have_info ? ptd->info : "", have_info ? ")" : "", have_bt ? "\n" : "", have_bt ? ptd->backtrace->str : "", have_bt && *left > 0 ? "\n" : "");
	}
}

static void
dump_by_backtrace (GHashTable *ptrs)
{
	GHashTable *by_bt = g_hash_table_new (by_backtrace_hash, by_backtrace_equal);
	GHashTableIter iter;
	gpointer key, value;
	struct ptr_data *ptd;
	guint count;

	g_hash_table_iter_init (&iter, ptrs);
	while (g_hash_table_iter_next (&iter, &key, &value)) {
		guint cnt;

		ptd = value;
		if (!ptd)
			continue;

		cnt = GPOINTER_TO_UINT (g_hash_table_lookup (by_bt, ptd));
		cnt++;

		g_hash_table_insert (by_bt, ptd, GUINT_TO_POINTER (cnt));
	}

	count = g_hash_table_size (by_bt);
	g_hash_table_foreach (by_bt, dump_by_backtrace_cb, &count);
	g_hash_table_destroy (by_bt);
}
#endif /* HAVE_BACKTRACE_SYMBOLS */

static void
dump_tracked_ptrs (gboolean is_at_exit)
{
	G_LOCK (ptr_tracker);

	if (ptr_tracker) {
		g_print ("\n----------------------------------------------------------\n");
		if (g_hash_table_size (ptr_tracker) == 0) {
			g_print ("   All tracked pointers were properly removed\n");
		} else {
			guint count = g_hash_table_size (ptr_tracker);
			g_print ("   Left %d tracked pointers:\n", count);
			#ifdef HAVE_BACKTRACE_SYMBOLS
			dump_by_backtrace (ptr_tracker);
			#else
			g_hash_table_foreach (ptr_tracker, dump_left_ptrs_cb, &count);
			#endif
		}
		g_print ("----------------------------------------------------------\n");
	} else if (!is_at_exit) {
		g_print ("\n----------------------------------------------------------\n");
		g_print ("   Did not track any pointers yet\n");
		g_print ("----------------------------------------------------------\n");
	}

	G_UNLOCK (ptr_tracker);
}

#ifdef HAVE_BACKTRACE_SYMBOLS

#ifdef HAVE_ELFUTILS_LIBDWFL
static Dwfl *
dwfl_get (gboolean reload)
{
	static gchar *debuginfo_path = NULL;
	static Dwfl *dwfl = NULL;
	static gboolean checked_for_dwfl = FALSE;
	static GMutex dwfl_mutex;
	static const Dwfl_Callbacks proc_callbacks = {
		.find_debuginfo = dwfl_standard_find_debuginfo,
		.debuginfo_path = &debuginfo_path,
		.find_elf = dwfl_linux_proc_find_elf
	};

	g_mutex_lock (&dwfl_mutex);

	if (checked_for_dwfl) {
		if (!reload) {
			g_mutex_unlock (&dwfl_mutex);
			return dwfl;
		}

		dwfl_end (dwfl);
		dwfl = NULL;
	}

	checked_for_dwfl = TRUE;

	dwfl = dwfl_begin (&proc_callbacks);
	if (!dwfl) {
		g_mutex_unlock (&dwfl_mutex);
		return NULL;
	}

	errno = 0;
	if (dwfl_linux_proc_report (dwfl, getpid ()) != 0 || dwfl_report_end (dwfl, NULL, NULL) != 0) {
		dwfl_end (dwfl);
		dwfl = NULL;
	}

	g_mutex_unlock (&dwfl_mutex);

	return dwfl;
}

struct getmodules_callback_arg
{
	gpointer addr;
	const gchar *func_name;
	const gchar *file_path;
	gint lineno;
};

static gint
getmodules_callback (Dwfl_Module *module,
                     gpointer *module_userdata_pointer,
                     const gchar *module_name,
                     Dwarf_Addr module_low_addr,
                     gpointer arg_voidp)
{
	struct getmodules_callback_arg *arg = arg_voidp;
	Dwfl_Line *line;

	arg->func_name = dwfl_module_addrname (module, (GElf_Addr) arg->addr);
	line = dwfl_module_getsrc (module, (GElf_Addr) arg->addr);
	if (line) {
		arg->file_path = dwfl_lineinfo (line, NULL, &arg->lineno, NULL, NULL, NULL);
	} else {
		arg->file_path = NULL;
	}

	return arg->func_name ? DWARF_CB_ABORT : DWARF_CB_OK;
}
#endif /* HAVE_ELFUTILS_LIBDWFL */

static const gchar *
addr_lookup (gpointer addr,
             const gchar **file_path,
             gint *lineno,
             const gchar *fallback)
{
#ifdef HAVE_ELFUTILS_LIBDWFL
	Dwfl *dwfl = dwfl_get (FALSE);
	struct getmodules_callback_arg arg;

	if (!dwfl)
		return NULL;

	arg.addr = addr;
	arg.func_name = NULL;
	arg.file_path = NULL;
	arg.lineno = -1;

	dwfl_getmodules (dwfl, getmodules_callback, &arg, 0);

	if (!arg.func_name && fallback && strstr (fallback, "/lib") != fallback && strstr (fallback, "/usr/lib") != fallback) {
		dwfl = dwfl_get (TRUE);
		if (dwfl)
			dwfl_getmodules (dwfl, getmodules_callback, &arg, 0);
	}

	*file_path = arg.file_path;
	*lineno = arg.lineno;

	return arg.func_name;
#else /* HAVE_ELFUTILS_LIBDWFL */
	return NULL;
#endif /* HAVE_ELFUTILS_LIBDWFL */
}

#endif /* HAVE_BACKTRACE_SYMBOLS */

static GString *
get_current_backtrace (void)
{
#ifdef HAVE_BACKTRACE_SYMBOLS
	#define MAX_BT_DEPTH 50
	gint nptrs, ii;
	gpointer bt[MAX_BT_DEPTH + 1];
	gchar **bt_syms;
	GString *bt_str;

	nptrs = backtrace (bt, MAX_BT_DEPTH + 1);
	if (nptrs <= 2)
		return NULL;

	bt_syms = backtrace_symbols (bt, nptrs);
	if (!bt_syms)
		return NULL;

	bt_str = g_string_new ("");
	for (ii = 2; ii < nptrs; ii++) {
		gint lineno = -1;
		const gchar *file_path = NULL;
		const gchar *str = addr_lookup (bt[ii], &file_path, &lineno, bt_syms[ii]);
		if (!str) {
			str = bt_syms[ii];
			file_path = NULL;
			lineno = -1;
		}
		if (!str)
			continue;

		if (bt_str->len)
			g_string_append (bt_str, "\n\t   by ");
		g_string_append (bt_str, str);
		if (str != bt_syms[ii])
			g_string_append (bt_str, "()");

		if (file_path && lineno > 0) {
			const gchar *lastsep = strrchr (file_path, G_DIR_SEPARATOR);
			g_string_append_printf (bt_str, " at %s:%d", lastsep ? lastsep + 1 : file_path, lineno);
		}
	}

	g_free (bt_syms);

	if (bt_str->len == 0) {
		g_string_free (bt_str, TRUE);
		bt_str = NULL;
	} else {
		g_string_insert (bt_str, 0, "\t   at ");
	}

	return bt_str;

	#undef MAX_BT_DEPTH
#else /* HAVE_BACKTRACE_SYMBOLS */
	return NULL;
#endif /* HAVE_BACKTRACE_SYMBOLS */
}

static void
dump_left_at_exit_cb (void)
{
	dump_tracked_ptrs (TRUE);

	G_LOCK (ptr_tracker);
	if (ptr_tracker) {
		g_hash_table_destroy (ptr_tracker);
		ptr_tracker = NULL;
	}
	G_UNLOCK (ptr_tracker);
}

/**
 * camel_pointer_tracker_track_with_info:
 * @ptr: pointer to add to the pointer tracker
 * @info: info to print in tracker summary
 *
 * Adds pointer to the pointer tracker, with associated information,
 * which is printed in summary of pointer tracker printed by
 * camel_pointer_tracker_dump(). For convenience can be used
 * camel_pointer_tracker_track(), which adds place of the caller
 * as @info. Added pointer should be removed with pair function
 * camel_pointer_tracker_untrack().
 *
 * Since: 3.6
 **/
void
camel_pointer_tracker_track_with_info (gpointer ptr,
                                       const gchar *info)
{
	struct pt_data *ptd;

	g_return_if_fail (ptr != NULL);

	G_LOCK (ptr_tracker);
	if (!ptr_tracker) {
		ptr_tracker = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, free_pt_data);
		atexit (dump_left_at_exit_cb);
	}

	ptd = g_new0 (struct pt_data, 1);
	ptd->ptr = ptr;
	ptd->info = g_strdup (info);
	ptd->backtrace = get_current_backtrace ();

	g_hash_table_insert (ptr_tracker, ptr, ptd);

	G_UNLOCK (ptr_tracker);
}

/**
 * camel_pointer_tracker_untrack:
 * @ptr: pointer to remove from the tracker
 *
 * Removes pointer from the pointer tracker. It's an error to try
 * to remove pointer which was not added to the tracker by
 * camel_pointer_tracker_track() or camel_pointer_tracker_track_with_info(),
 * or a pointer which was already removed.
 *
 * Since: 3.6
 **/
void
camel_pointer_tracker_untrack (gpointer ptr)
{
	g_return_if_fail (ptr != NULL);

	G_LOCK (ptr_tracker);

	if (!ptr_tracker)
		g_printerr ("Pointer tracker not initialized, thus cannot remove %p\n", ptr);
	else if (!g_hash_table_lookup (ptr_tracker, ptr))
		g_printerr ("Pointer %p is not tracked\n", ptr);
	else
		g_hash_table_remove (ptr_tracker, ptr);

	G_UNLOCK (ptr_tracker);
}

/**
 * camel_pointer_tracker_dump:
 *
 * Prints information about currently stored pointers
 * in the pointer tracker. This is called automatically
 * on application exit if camel_pointer_tracker_track() or
 * camel_pointer_tracker_track_with_info() was called.
 *
 * Note: If the library is configured with --enable-backtraces,
 * then also backtraces where the pointer was added is printed
 * in the summary.
 *
 * Since: 3.6
 **/
void
camel_pointer_tracker_dump (void)
{
	dump_tracked_ptrs (FALSE);
}

/**
 * camel_debug_get_backtrace:
 *
 * Gets current backtrace leading to this function call.
 *
 * Returns: Current backtrace, or %NULL, if cannot determine it.
 *
 * Note: Getting backtraces only works if the library was
 * configured with --enable-backtraces.
 *
 * Since: 3.12
 **/
GString *
camel_debug_get_backtrace (void)
{
	return get_current_backtrace ();
}
