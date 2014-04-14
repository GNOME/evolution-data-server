/*
 * e-gdbus-templates.h
 *
 * Copyright (C) 2011 Red Hat, Inc. (www.redhat.com)
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

#if !defined (__LIBEDATASERVER_H_INSIDE__) && !defined (LIBEDATASERVER_COMPILATION)
#error "Only <libedataserver/libedataserver.h> should be included directly."
#endif

#ifndef EDS_DISABLE_DEPRECATED

/* Do not generate bindings. */
#ifndef __GI_SCANNER__

#ifndef E_GDBUS_TEMPLATES_H
#define E_GDBUS_TEMPLATES_H

#include <gio/gio.h>

G_BEGIN_DECLS

void		e_gdbus_templates_init_main_thread		(void);

#define E_TYPE_GDBUS_ASYNC_OP_KEEPER		(e_gdbus_async_op_keeper_get_type ())
#define E_GDBUS_ASYNC_OP_KEEPER(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), E_TYPE_GDBUS_ASYNC_OP_KEEPER, EGdbusAsyncOpKeeper))
#define E_IS_GDBUS_ASYNC_OP_KEEPER(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), E_TYPE_GDBUS_ASYNC_OP_KEEPER))
#define E_GDBUS_ASYNC_OP_KEEPER_GET_IFACE(o)	(G_TYPE_INSTANCE_GET_INTERFACE((o), E_TYPE_GDBUS_ASYNC_OP_KEEPER, EGdbusAsyncOpKeeperInterface))

typedef struct _EGdbusAsyncOpKeeper EGdbusAsyncOpKeeper; /* Dummy typedef */
typedef struct _EGdbusAsyncOpKeeperInterface EGdbusAsyncOpKeeperInterface;

struct _EGdbusAsyncOpKeeperInterface
{
	GTypeInterface parent_iface;

	GHashTable *	(* get_pending_ops)	(EGdbusAsyncOpKeeper *object);
	gboolean	(* cancel_op_sync)	(EGdbusAsyncOpKeeper *object, guint in_opid, GCancellable *cancellable, GError **error);
};

GType e_gdbus_async_op_keeper_get_type (void) G_GNUC_CONST;

GHashTable *	e_gdbus_async_op_keeper_create_pending_ops	(EGdbusAsyncOpKeeper *object);
GHashTable *	e_gdbus_async_op_keeper_get_pending_ops		(EGdbusAsyncOpKeeper *object);
gboolean	e_gdbus_async_op_keeper_cancel_op_sync		(EGdbusAsyncOpKeeper *object, guint in_opid, GCancellable *cancellable, GError **error);

enum {
	E_GDBUS_TYPE_IS_ASYNC	= 1 << 0, /* if set, then OPID and GError precedes to actual parameter */
	E_GDBUS_TYPE_VOID = 1 << 1,
	E_GDBUS_TYPE_BOOLEAN = 1 << 2,
	E_GDBUS_TYPE_STRING = 1 << 3,
	E_GDBUS_TYPE_STRV = 1 << 4,
	E_GDBUS_TYPE_UINT = 1 << 5
};

/* _where is a target component name, like ' ## _where ## ' or 'cal'
 * _mname is method name, like 'open'
 * _mtype is method type, like 'method_in'
 * _param_name is parameter name, like 'only_if_exists'
 * _param_type is parameter type, as string, like "s"
 * all except _param_type are identificators, not strings
*/
#define E_DECLARE_GDBUS_ARG(_where, _mname, _mtype, _param_name, _param_type) \
	static const GDBusArgInfo e_gdbus_ ## _where ## _ ## _mtype ## _mname ## _param_name = \
	{ \
		-1, \
		(gchar *) # _param_name, \
		(gchar *) _param_type, \
		(GDBusAnnotationInfo **) NULL, \
	};

#define E_DECLARE_GDBUS_NOTIFY_SIGNAL_0(_where, _sname) \
	static const GDBusSignalInfo e_gdbus_ ## _where ## _signal_ ## _sname = \
	{ \
		-1, \
		(gchar *) # _sname, \
		(GDBusArgInfo **) NULL, \
		(GDBusAnnotationInfo **) NULL, \
	};

#define E_DECLARE_GDBUS_NOTIFY_SIGNAL_1(_where, _sname, _p1_name, _p1_type) \
	E_DECLARE_GDBUS_ARG (_where, _sname, signal, _p1_name, _p1_type) \
 \
	static const GDBusArgInfo * const e_gdbus_ ## _where ## _signal_arg_pointers_ ## _sname[] = \
	{ \
		&e_gdbus_ ## _where ## _signal ## _sname ## _p1_name, \
		NULL \
	}; \
 \
	static const GDBusSignalInfo e_gdbus_ ## _where ## _signal_ ## _sname = \
	{ \
		-1, \
		(gchar *) # _sname, \
		(GDBusArgInfo **) &e_gdbus_ ## _where ## _signal_arg_pointers_ ## _sname, \
		(GDBusAnnotationInfo **) NULL, \
	};

#define E_DECLARE_GDBUS_NOTIFY_SIGNAL_2(_where, _sname, _p1_name, _p1_type, _p2_name, _p2_type) \
	E_DECLARE_GDBUS_ARG (_where, _sname, signal, _p1_name, _p1_type) \
	E_DECLARE_GDBUS_ARG (_where, _sname, signal, _p2_name, _p2_type) \
 \
	static const GDBusArgInfo * const e_gdbus_ ## _where ## _signal_arg_pointers_ ## _sname[] = \
	{ \
		&e_gdbus_ ## _where ## _signal ## _sname ## _p1_name, \
		&e_gdbus_ ## _where ## _signal ## _sname ## _p2_name, \
		NULL \
	}; \
 \
	static const GDBusSignalInfo e_gdbus_ ## _where ## _signal_ ## _sname = \
	{ \
		-1, \
		(gchar *) # _sname, \
		(GDBusArgInfo **) &e_gdbus_ ## _where ## _signal_arg_pointers_ ## _sname, \
		(GDBusAnnotationInfo **) NULL, \
	};

#define E_DECLARE_GDBUS_ASYNC_DONE_SIGNAL_0(_where, _sname) \
	E_DECLARE_GDBUS_ARG (_where, _sname, signal, opid, "u") \
	E_DECLARE_GDBUS_ARG (_where, _sname, signal, dbus_error_name, "s") \
	E_DECLARE_GDBUS_ARG (_where, _sname, signal, dbus_error_message, "s") \
 \
	static const GDBusArgInfo * const e_gdbus_ ## _where ## _signal_arg_pointers_ ## _sname[] = \
	{ \
		&e_gdbus_ ## _where ## _signal ## _sname ## opid, \
		&e_gdbus_ ## _where ## _signal ## _sname ## dbus_error_name, \
		&e_gdbus_ ## _where ## _signal ## _sname ## dbus_error_message, \
		NULL \
	}; \
 \
	static const GDBusSignalInfo e_gdbus_ ## _where ## _signal_ ## _sname = \
	{ \
		-1, \
		(gchar *) # _sname, \
		(GDBusArgInfo **) &e_gdbus_ ## _where ## _signal_arg_pointers_ ## _sname, \
		(GDBusAnnotationInfo **) NULL, \
	};

#define E_DECLARE_GDBUS_ASYNC_DONE_SIGNAL_1(_where, _sname, _p1_name, _p1_type) \
	E_DECLARE_GDBUS_ARG (_where, _sname, signal, opid, "u") \
	E_DECLARE_GDBUS_ARG (_where, _sname, signal, dbus_error_name, "s") \
	E_DECLARE_GDBUS_ARG (_where, _sname, signal, dbus_error_message, "s") \
	E_DECLARE_GDBUS_ARG (_where, _sname, signal, _p1_name, _p1_type) \
 \
	static const GDBusArgInfo * const e_gdbus_ ## _where ## _signal_arg_pointers_ ## _sname[] = \
	{ \
		&e_gdbus_ ## _where ## _signal ## _sname ## opid, \
		&e_gdbus_ ## _where ## _signal ## _sname ## dbus_error_name, \
		&e_gdbus_ ## _where ## _signal ## _sname ## dbus_error_message, \
		&e_gdbus_ ## _where ## _signal ## _sname ## _p1_name, \
		NULL \
	}; \
 \
	static const GDBusSignalInfo e_gdbus_ ## _where ## _signal_ ## _sname = \
	{ \
		-1, \
		(gchar *) # _sname, \
		(GDBusArgInfo **) &e_gdbus_ ## _where ## _signal_arg_pointers_ ## _sname, \
		(GDBusAnnotationInfo **) NULL, \
	};

#define E_DECLARE_GDBUS_SYNC_METHOD_0(_where, _mname) \
	static const GDBusMethodInfo e_gdbus_ ## _where ## _method_ ## _mname = \
	{ \
		-1, \
		(gchar *) # _mname, \
		(GDBusArgInfo **) NULL, \
		(GDBusArgInfo **) NULL, \
		(GDBusAnnotationInfo **) NULL, \
	};

#define E_DECLARE_GDBUS_SYNC_METHOD_1(_where, _mname, _p1_name, _p1_type) \
	E_DECLARE_GDBUS_ARG (_where, _mname, method_in, _p1_name, _p1_type) \
 \
	static const GDBusArgInfo * const e_gdbus_ ## _where ## _method_in_arg_pointers_ ## _mname[] = \
	{ \
		&e_gdbus_ ## _where ## _method_in ## _mname ## _p1_name, \
		NULL \
	}; \
 \
	static const GDBusMethodInfo e_gdbus_ ## _where ## _method_ ## _mname = \
	{ \
		-1, \
		(gchar *) # _mname, \
		(GDBusArgInfo **) &e_gdbus_ ## _where ## _method_in_arg_pointers_ ## _mname, \
		(GDBusArgInfo **) NULL, \
		(GDBusAnnotationInfo **) NULL, \
	};

#define E_DECLARE_GDBUS_SYNC_METHOD_0_WITH_RETURN(_where, _mname, _r1_name, _r1_type) \
	E_DECLARE_GDBUS_ARG (_where, _mname, method_out, _r1_name, _r1_type) \
 \
	static const GDBusArgInfo * const e_gdbus_ ## _where ## _method_out_arg_pointers_ ## _mname[] = \
	{ \
		&e_gdbus_ ## _where ## _method_out ## _mname ## _r1_name, \
		NULL \
	}; \
 \
	static const GDBusMethodInfo e_gdbus_ ## _where ## _method_ ## _mname = \
	{ \
		-1, \
		(gchar *) # _mname, \
		(GDBusArgInfo **) NULL, \
		(GDBusArgInfo **) &e_gdbus_ ## _where ## _method_out_arg_pointers_ ## _mname, \
		(GDBusAnnotationInfo **) NULL, \
	};

#define E_DECLARE_GDBUS_SYNC_METHOD_1_WITH_RETURN(_where, _mname, _p1_name, _p1_type, _r1_name, _r1_type)\
	E_DECLARE_GDBUS_ARG (_where, _mname, method_in, _p1_name, _p1_type) \
	E_DECLARE_GDBUS_ARG (_where, _mname, method_out, _r1_name, _r1_type) \
 \
	static const GDBusArgInfo * const e_gdbus_ ## _where ## _method_in_arg_pointers_ ## _mname[] = \
	{ \
		&e_gdbus_ ## _where ## _method_in ## _mname ## _p1_name, \
		NULL \
	}; \
 \
	static const GDBusArgInfo * const e_gdbus_ ## _where ## _method_out_arg_pointers_ ## _mname[] = \
	{ \
		&e_gdbus_ ## _where ## _method_out ## _mname ## _r1_name, \
		NULL \
	}; \
 \
	static const GDBusMethodInfo e_gdbus_ ## _where ## _method_ ## _mname = \
	{ \
		-1, \
		(gchar *) # _mname, \
		(GDBusArgInfo **) &e_gdbus_ ## _where ## _method_in_arg_pointers_ ## _mname, \
		(GDBusArgInfo **) &e_gdbus_ ## _where ## _method_out_arg_pointers_ ## _mname, \
		(GDBusAnnotationInfo **) NULL, \
	};

#define E_DECLARE_GDBUS_ASYNC_METHOD_0(_where, _mname) \
	E_DECLARE_GDBUS_ASYNC_DONE_SIGNAL_0 (_where, _mname ## _done) \
	E_DECLARE_GDBUS_ARG (_where, _mname, method_out, opid, "u") \
 \
	static const GDBusArgInfo * const e_gdbus_ ## _where ## _method_out_arg_pointers_ ## _mname[] = \
	{ \
		&e_gdbus_ ## _where ## _method_out ## _mname ## opid, \
		NULL \
	}; \
 \
	static const GDBusMethodInfo e_gdbus_ ## _where ## _method_ ## _mname = \
	{ \
		-1, \
		(gchar *) # _mname, \
		(GDBusArgInfo **) NULL, \
		(GDBusArgInfo **) &e_gdbus_ ## _where ## _method_out_arg_pointers_ ## _mname, \
		(GDBusAnnotationInfo **) NULL, \
	};

#define E_DECLARE_GDBUS_ASYNC_METHOD_1(_where, _mname, _p1_name, _p1_type) \
	E_DECLARE_GDBUS_ASYNC_DONE_SIGNAL_0 (_where, _mname ## _done) \
	E_DECLARE_GDBUS_ARG (_where, _mname, method_in, _p1_name, _p1_type) \
	E_DECLARE_GDBUS_ARG (_where, _mname, method_out, opid, "u") \
 \
	static const GDBusArgInfo * const e_gdbus_ ## _where ## _method_in_arg_pointers_ ## _mname[] = \
	{ \
		&e_gdbus_ ## _where ## _method_in ## _mname ## _p1_name, \
		NULL \
	}; \
 \
	static const GDBusArgInfo * const e_gdbus_ ## _where ## _method_out_arg_pointers_ ## _mname[] = \
	{ \
		&e_gdbus_ ## _where ## _method_out ## _mname ## opid, \
		NULL \
	}; \
 \
	static const GDBusMethodInfo e_gdbus_ ## _where ## _method_ ## _mname = \
	{ \
		-1, \
		(gchar *) # _mname, \
		(GDBusArgInfo **) &e_gdbus_ ## _where ## _method_in_arg_pointers_ ## _mname, \
		(GDBusArgInfo **) &e_gdbus_ ## _where ## _method_out_arg_pointers_ ## _mname, \
		(GDBusAnnotationInfo **) NULL, \
	};

#define E_DECLARE_GDBUS_ASYNC_METHOD_0_WITH_RETURN(_where, _mname, _r1_name, _r1_type) \
	E_DECLARE_GDBUS_ASYNC_DONE_SIGNAL_1 (_where, _mname ## _done, _r1_name, _r1_type) \
	E_DECLARE_GDBUS_ARG (_where, _mname, method_out, opid, "u") \
 \
	static const GDBusArgInfo * const e_gdbus_ ## _where ## _method_out_arg_pointers_ ## _mname[] = \
	{ \
		&e_gdbus_ ## _where ## _method_out ## _mname ## opid, \
		NULL \
	}; \
 \
	static const GDBusMethodInfo e_gdbus_ ## _where ## _method_ ## _mname = \
	{ \
		-1, \
		(gchar *) # _mname, \
		(GDBusArgInfo **) NULL, \
		(GDBusArgInfo **) &e_gdbus_ ## _where ## _method_out_arg_pointers_ ## _mname, \
		(GDBusAnnotationInfo **) NULL, \
	};

#define E_DECLARE_GDBUS_ASYNC_METHOD_1_WITH_RETURN(_where, _mname, _p1_name, _p1_type, _r1_name, _r1_type)\
	E_DECLARE_GDBUS_ASYNC_DONE_SIGNAL_1 (_where, _mname ## _done, _r1_name, _r1_type) \
	E_DECLARE_GDBUS_ARG (_where, _mname, method_in, _p1_name, _p1_type) \
	E_DECLARE_GDBUS_ARG (_where, _mname, method_out, opid, "u") \
 \
	static const GDBusArgInfo * const e_gdbus_ ## _where ## _method_in_arg_pointers_ ## _mname[] = \
	{ \
		&e_gdbus_ ## _where ## _method_in ## _mname ## _p1_name, \
		NULL \
	}; \
 \
	static const GDBusArgInfo * const e_gdbus_ ## _where ## _method_out_arg_pointers_ ## _mname[] = \
	{ \
		&e_gdbus_ ## _where ## _method_out ## _mname ## opid, \
		NULL \
	}; \
 \
	static const GDBusMethodInfo e_gdbus_ ## _where ## _method_ ## _mname = \
	{ \
		-1, \
		(gchar *) # _mname, \
		(GDBusArgInfo **) &e_gdbus_ ## _where ## _method_in_arg_pointers_ ## _mname, \
		(GDBusArgInfo **) &e_gdbus_ ## _where ## _method_out_arg_pointers_ ## _mname, \
		(GDBusAnnotationInfo **) NULL, \
	};

#define E_DECLARED_GDBUS_SIGNAL_INFO_NAME(_where, _sname) e_gdbus_ ## _where ## _signal_ ## _sname
#define E_DECLARED_GDBUS_METHOD_INFO_NAME(_where, _mname) e_gdbus_ ## _where ## _method_ ## _mname

/* this requires signal_emission_hook_cb_ ## _sig_name hook defined,
 * which can be created with one of E_DECLARE_GDBUS_SIGNAL_EMISSION_HOOK_... macros */
#define E_INIT_GDBUS_SIGNAL_VOID(_iface_struct, _dbus_sig_name_str, _sig_name_var, _sig_id) \
	signals[_sig_id] = g_signal_new (# _sig_name_var, \
			G_TYPE_FROM_INTERFACE (iface), \
			G_SIGNAL_RUN_LAST, \
			G_STRUCT_OFFSET (_iface_struct, _sig_name_var), \
			NULL, \
			NULL, \
			NULL, \
			G_TYPE_NONE, \
			0); \
 \
	g_signal_add_emission_hook (signals[_sig_id], 0, signal_emission_hook_cb_ ## _sig_name_var, (gpointer) _dbus_sig_name_str, NULL);\
	g_hash_table_insert (_signal_name_to_id, (gpointer) _dbus_sig_name_str, GUINT_TO_POINTER (_sig_id)); \
	g_hash_table_insert (_signal_name_to_type, (gpointer) _dbus_sig_name_str, GUINT_TO_POINTER (E_GDBUS_TYPE_VOID));

#define E_INIT_GDBUS_SIGNAL_TMPL_TYPED(_mtype, _gtype, _iface_struct, _dbus_sig_name_str, _sig_name_var, _sig_id) \
	signals[_sig_id] = g_signal_new (# _sig_name_var, \
			G_TYPE_FROM_INTERFACE (iface), \
			G_SIGNAL_RUN_LAST, \
			G_STRUCT_OFFSET (_iface_struct, _sig_name_var), \
			NULL, \
			NULL, \
			NULL, \
			G_TYPE_NONE, \
			1, \
			G_TYPE_ ## _gtype); \
 \
	g_signal_add_emission_hook (signals[_sig_id], 0, signal_emission_hook_cb_ ## _sig_name_var, (gpointer) _dbus_sig_name_str, NULL);\
	g_hash_table_insert (_signal_name_to_id, (gpointer) _dbus_sig_name_str, GUINT_TO_POINTER (_sig_id)); \
	g_hash_table_insert (_signal_name_to_type, (gpointer) _dbus_sig_name_str, GUINT_TO_POINTER (E_GDBUS_TYPE_ ## _gtype));

#define E_INIT_GDBUS_SIGNAL_BOOLEAN(_iface_struct, _dbus_sig_name_str, _sig_name_var, _sig_id) \
	E_INIT_GDBUS_SIGNAL_TMPL_TYPED (BOOLEAN, BOOLEAN, _iface_struct, _dbus_sig_name_str, _sig_name_var, _sig_id)

#define E_INIT_GDBUS_SIGNAL_STRING(_iface_struct, _dbus_sig_name_str, _sig_name_var, _sig_id) \
	E_INIT_GDBUS_SIGNAL_TMPL_TYPED (STRING, STRING, _iface_struct, _dbus_sig_name_str, _sig_name_var, _sig_id)

#define E_INIT_GDBUS_SIGNAL_STRV(_iface_struct, _dbus_sig_name_str, _sig_name_var, _sig_id) \
	E_INIT_GDBUS_SIGNAL_TMPL_TYPED (BOXED, STRV, _iface_struct, _dbus_sig_name_str, _sig_name_var, _sig_id)

#define E_INIT_GDBUS_SIGNAL_UINT(_iface_struct, _dbus_sig_name_str, _sig_name_var, _sig_id) \
	E_INIT_GDBUS_SIGNAL_TMPL_TYPED (UINT, UINT, _iface_struct, _dbus_sig_name_str, _sig_name_var, _sig_id)

#define E_INIT_GDBUS_SIGNAL_UINT_STRING(_iface_struct, _dbus_sig_name_str, _sig_name_var, _sig_id) \
	signals[_sig_id] = g_signal_new (# _sig_name_var, \
			G_TYPE_FROM_INTERFACE (iface), \
			G_SIGNAL_RUN_LAST, \
			G_STRUCT_OFFSET (_iface_struct, _sig_name_var), \
			NULL, \
			NULL, \
			NULL, \
			G_TYPE_NONE, \
			2, \
			G_TYPE_UINT, G_TYPE_STRING); \
 \
	g_signal_add_emission_hook (signals[_sig_id], 0, signal_emission_hook_cb_ ## _sig_name_var, (gpointer) _dbus_sig_name_str, NULL);\
	g_hash_table_insert (_signal_name_to_id, (gpointer) _dbus_sig_name_str, GUINT_TO_POINTER (_sig_id)); \
	g_hash_table_insert (_signal_name_to_type, (gpointer) _dbus_sig_name_str, GUINT_TO_POINTER (E_GDBUS_TYPE_UINT | E_GDBUS_TYPE_STRING));

#define E_INIT_GDBUS_METHOD_DONE_VOID(_iface_struct, _sig_name_var, _done_sig_id) \
	signals[_done_sig_id] = g_signal_new (# _sig_name_var "_done", \
			G_TYPE_FROM_INTERFACE (iface), \
			G_SIGNAL_RUN_LAST, \
			G_STRUCT_OFFSET (_iface_struct, _sig_name_var ## _done), \
			NULL, \
			NULL, \
			NULL, \
			G_TYPE_NONE, \
			2, \
			G_TYPE_UINT, G_TYPE_ERROR); \
	g_signal_add_emission_hook (signals[_done_sig_id], 0, \
			signal_emission_hook_cb_ ## _sig_name_var ## _done, (gpointer) # _sig_name_var "_done", NULL); \
	g_hash_table_insert (_signal_name_to_id, (gpointer) # _sig_name_var "_done", GUINT_TO_POINTER (_done_sig_id)); \
	g_hash_table_insert (_signal_name_to_type, (gpointer) # _sig_name_var "_done", GUINT_TO_POINTER (E_GDBUS_TYPE_IS_ASYNC | E_GDBUS_TYPE_VOID));

#define E_INIT_GDBUS_METHOD_DONE_ASYNC_TMPL_TYPED(_mtype, _gtype, _iface_struct, _sig_name_var, _done_sig_id) \
	signals[_done_sig_id] = g_signal_new (# _sig_name_var "_done", \
			G_TYPE_FROM_INTERFACE (iface), \
			G_SIGNAL_RUN_LAST, \
			G_STRUCT_OFFSET (_iface_struct, _sig_name_var ## _done), \
			NULL, \
			NULL, \
			NULL, \
			G_TYPE_NONE, \
			3, \
			G_TYPE_UINT, G_TYPE_ERROR, G_TYPE_ ## _gtype); \
	g_signal_add_emission_hook (signals[_done_sig_id], 0, \
			signal_emission_hook_cb_ ## _sig_name_var ## _done, (gpointer) # _sig_name_var "_done", NULL); \
	g_hash_table_insert (_signal_name_to_id, (gpointer) # _sig_name_var "_done", GUINT_TO_POINTER (_done_sig_id)); \
	g_hash_table_insert (_signal_name_to_type, (gpointer) # _sig_name_var "_done", GUINT_TO_POINTER (E_GDBUS_TYPE_IS_ASYNC | E_GDBUS_TYPE_ ## _gtype));

#define E_INIT_GDBUS_METHOD_DONE_BOOLEAN(_iface_struct, _sig_name_var, _done_sig_id) \
	E_INIT_GDBUS_METHOD_DONE_ASYNC_TMPL_TYPED (BOOLEAN, BOOLEAN, _iface_struct, _sig_name_var, _done_sig_id)

#define E_INIT_GDBUS_METHOD_DONE_STRING(_iface_struct, _sig_name_var, _done_sig_id) \
	E_INIT_GDBUS_METHOD_DONE_ASYNC_TMPL_TYPED (STRING, STRING, _iface_struct, _sig_name_var, _done_sig_id)

#define E_INIT_GDBUS_METHOD_DONE_STRV(_iface_struct, _sig_name_var, _done_sig_id) \
	E_INIT_GDBUS_METHOD_DONE_ASYNC_TMPL_TYPED (BOXED, STRV, _iface_struct, _sig_name_var, _done_sig_id)

#define E_INIT_GDBUS_METHOD_DONE_UINT(_iface_struct, _sig_name_var, _done_sig_id) \
	E_INIT_GDBUS_METHOD_DONE_ASYNC_TMPL_TYPED (UINT, UINT, _iface_struct, _sig_name_var, _done_sig_id)

/* do not use this directly, there is missing _method_name_to_type insertion */
#define E_INIT_GDBUS_METHOD_CALL_TMPL_VOID(_iface_struct, _dbus_sig_name_str, _sig_name_var, _method_sig_id) \
	signals[_method_sig_id] = g_signal_new ("handle-" # _sig_name_var, \
			G_TYPE_FROM_INTERFACE (iface), \
			G_SIGNAL_RUN_LAST, \
			G_STRUCT_OFFSET (_iface_struct, handle_ ## _sig_name_var), \
			NULL, NULL, NULL, \
			G_TYPE_BOOLEAN, \
			1, \
			G_TYPE_DBUS_METHOD_INVOCATION); \
	g_hash_table_insert (_method_name_to_id, (gpointer) _dbus_sig_name_str, GUINT_TO_POINTER (_method_sig_id));

/* do not use this directly, there is missing _method_name_to_type insertion */
#define E_INIT_GDBUS_METHOD_CALL_TMPL_TYPED(_mtype, _gtype, _iface_struct, _dbus_sig_name_str, _sig_name_var, _method_sig_id) \
	signals[_method_sig_id] = g_signal_new ("handle-" # _sig_name_var, \
			G_TYPE_FROM_INTERFACE (iface), \
			G_SIGNAL_RUN_LAST, \
			G_STRUCT_OFFSET (_iface_struct, handle_ ## _sig_name_var), \
			NULL, NULL, NULL, \
			G_TYPE_BOOLEAN, \
			2, \
			G_TYPE_DBUS_METHOD_INVOCATION, \
			G_TYPE_ ## _gtype); \
	g_hash_table_insert (_method_name_to_id, (gpointer) _dbus_sig_name_str, GUINT_TO_POINTER (_method_sig_id)); \

#define E_INIT_GDBUS_METHOD_CALL_ASYNC_TMPL_TYPED(_mtype, _gtype, _iface_struct, _dbus_sig_name_str, _sig_name_var, _method_sig_id) \
	E_INIT_GDBUS_METHOD_CALL_TMPL_TYPED (_mtype, _gtype, _iface_struct, _dbus_sig_name_str, _sig_name_var, _method_sig_id) \
	g_hash_table_insert (_method_name_to_type, (gpointer) _dbus_sig_name_str, GUINT_TO_POINTER (E_GDBUS_TYPE_ ## _gtype | E_GDBUS_TYPE_IS_ASYNC));

#define E_INIT_GDBUS_METHOD_VOID(_iface_struct, _dbus_sig_name_str, _sig_name_var, _method_sig_id) \
	E_INIT_GDBUS_METHOD_CALL_TMPL_VOID (_iface_struct, _dbus_sig_name_str, _sig_name_var, _method_sig_id) \
	g_hash_table_insert (_method_name_to_type, (gpointer) _dbus_sig_name_str, GUINT_TO_POINTER (E_GDBUS_TYPE_VOID));

#define E_INIT_GDBUS_METHOD_BOOLEAN(_iface_struct, _dbus_sig_name_str, _sig_name_var, _method_sig_id) \
	E_INIT_GDBUS_METHOD_CALL_TMPL_TYPED (BOOLEAN, BOOLEAN, _iface_struct, _dbus_sig_name_str, _sig_name_var, _method_sig_id)\
	g_hash_table_insert (_method_name_to_type, (gpointer) _dbus_sig_name_str, GUINT_TO_POINTER (E_GDBUS_TYPE_BOOLEAN));

#define E_INIT_GDBUS_METHOD_STRING(_iface_struct, _dbus_sig_name_str, _sig_name_var, _method_sig_id) \
	E_INIT_GDBUS_METHOD_CALL_TMPL_TYPED (STRING, STRING, _iface_struct, _dbus_sig_name_str, _sig_name_var, _method_sig_id) \
	g_hash_table_insert (_method_name_to_type, (gpointer) _dbus_sig_name_str, GUINT_TO_POINTER (E_GDBUS_TYPE_STRING));

#define E_INIT_GDBUS_METHOD_STRV(_iface_struct, _dbus_sig_name_str, _sig_name_var, _method_sig_id) \
	E_INIT_GDBUS_METHOD_CALL_TMPL_TYPED (BOXED, STRV, _iface_struct, _dbus_sig_name_str, _sig_name_var, _method_sig_id) \
	g_hash_table_insert (_method_name_to_type, (gpointer) _dbus_sig_name_str, GUINT_TO_POINTER (E_GDBUS_TYPE_STRV));

#define E_INIT_GDBUS_METHOD_UINT(_iface_struct, _dbus_sig_name_str, _sig_name_var, _method_sig_id) \
	E_INIT_GDBUS_METHOD_CALL_TMPL_TYPED (UINT, UINT, _iface_struct, _dbus_sig_name_str, _sig_name_var, _method_sig_id) \
	g_hash_table_insert (_method_name_to_type, (gpointer) _dbus_sig_name_str, GUINT_TO_POINTER (E_GDBUS_TYPE_UINT));

#define E_INIT_GDBUS_METHOD_CALL_VOID(_iface_struct, _dbus_sig_name_str, _sig_name_var, _method_sig_id) \
	E_INIT_GDBUS_METHOD_CALL_TMPL_VOID (_iface_struct, _dbus_sig_name_str, _sig_name_var, _method_sig_id) \
	g_hash_table_insert (_method_name_to_type, (gpointer) _dbus_sig_name_str, GUINT_TO_POINTER (E_GDBUS_TYPE_VOID | E_GDBUS_TYPE_IS_ASYNC));

#define E_INIT_GDBUS_METHOD_CALL_BOOLEAN(_iface_struct, _dbus_sig_name_str, _sig_name_var, _method_sig_id) \
	E_INIT_GDBUS_METHOD_CALL_ASYNC_TMPL_TYPED (BOOLEAN, BOOLEAN, _iface_struct, _dbus_sig_name_str, _sig_name_var, _method_sig_id)

#define E_INIT_GDBUS_METHOD_CALL_STRING(_iface_struct, _dbus_sig_name_str, _sig_name_var, _method_sig_id) \
	E_INIT_GDBUS_METHOD_CALL_ASYNC_TMPL_TYPED (STRING, STRING, _iface_struct, _dbus_sig_name_str, _sig_name_var, _method_sig_id)

#define E_INIT_GDBUS_METHOD_CALL_STRV(_iface_struct, _dbus_sig_name_str, _sig_name_var, _method_sig_id) \
	E_INIT_GDBUS_METHOD_CALL_ASYNC_TMPL_TYPED (BOXED, STRV, _iface_struct, _dbus_sig_name_str, _sig_name_var, _method_sig_id)

#define E_INIT_GDBUS_METHOD_CALL_UINT(_iface_struct, _dbus_sig_name_str, _sig_name_var, _method_sig_id) \
	E_INIT_GDBUS_METHOD_CALL_ASYNC_TMPL_TYPED (UINT, UINT, _iface_struct, _dbus_sig_name_str, _sig_name_var, _method_sig_id)

#define E_INIT_GDBUS_METHOD_ASYNC_VOID__VOID(_iface_struct, _dbus_sig_name_str, _sig_name_var, _method_sig_id, _done_sig_id) \
	E_INIT_GDBUS_METHOD_CALL_VOID (_iface_struct, _dbus_sig_name_str, _sig_name_var, _method_sig_id) \
	E_INIT_GDBUS_METHOD_DONE_VOID (_iface_struct, _sig_name_var, _done_sig_id)

#define E_INIT_GDBUS_METHOD_ASYNC_VOID__STRING(_iface_struct, _dbus_sig_name_str, _sig_name_var, _method_sig_id, _done_sig_id) \
	E_INIT_GDBUS_METHOD_CALL_VOID   (_iface_struct, _dbus_sig_name_str, _sig_name_var, _method_sig_id) \
	E_INIT_GDBUS_METHOD_DONE_STRING (_iface_struct, _sig_name_var, _done_sig_id)

#define E_INIT_GDBUS_METHOD_ASYNC_VOID__STRV(_iface_struct, _dbus_sig_name_str, _sig_name_var, _method_sig_id, _done_sig_id) \
	E_INIT_GDBUS_METHOD_CALL_VOID (_iface_struct, _dbus_sig_name_str, _sig_name_var, _method_sig_id) \
	E_INIT_GDBUS_METHOD_DONE_STRV (_iface_struct, _sig_name_var, _done_sig_id)

#define E_INIT_GDBUS_METHOD_ASYNC_BOOLEAN__VOID(_iface_struct, _dbus_sig_name_str, _sig_name_var, _method_sig_id, _done_sig_id) \
	E_INIT_GDBUS_METHOD_CALL_BOOLEAN (_iface_struct, _dbus_sig_name_str, _sig_name_var, _method_sig_id) \
	E_INIT_GDBUS_METHOD_DONE_VOID    (_iface_struct, _sig_name_var, _done_sig_id)

#define E_INIT_GDBUS_METHOD_ASYNC_UINT__VOID(_iface_struct, _dbus_sig_name_str, _sig_name_var, _method_sig_id, _done_sig_id) \
	E_INIT_GDBUS_METHOD_CALL_UINT (_iface_struct, _dbus_sig_name_str, _sig_name_var, _method_sig_id) \
	E_INIT_GDBUS_METHOD_DONE_VOID (_iface_struct, _sig_name_var, _done_sig_id)

#define E_INIT_GDBUS_METHOD_ASYNC_STRING__VOID(_iface_struct, _dbus_sig_name_str, _sig_name_var, _method_sig_id, _done_sig_id) \
	E_INIT_GDBUS_METHOD_CALL_STRING (_iface_struct, _dbus_sig_name_str, _sig_name_var, _method_sig_id) \
	E_INIT_GDBUS_METHOD_DONE_VOID   (_iface_struct, _sig_name_var, _done_sig_id)

#define E_INIT_GDBUS_METHOD_ASYNC_STRV__VOID(_iface_struct, _dbus_sig_name_str, _sig_name_var, _method_sig_id, _done_sig_id) \
	E_INIT_GDBUS_METHOD_CALL_STRV (_iface_struct, _dbus_sig_name_str, _sig_name_var, _method_sig_id) \
	E_INIT_GDBUS_METHOD_DONE_VOID (_iface_struct, _sig_name_var, _done_sig_id)

#define E_INIT_GDBUS_METHOD_ASYNC_STRING__STRING(_iface_struct, _dbus_sig_name_str, _sig_name_var, _method_sig_id, _done_sig_id)\
	E_INIT_GDBUS_METHOD_CALL_STRING (_iface_struct, _dbus_sig_name_str, _sig_name_var, _method_sig_id) \
	E_INIT_GDBUS_METHOD_DONE_STRING (_iface_struct, _sig_name_var, _done_sig_id)

#define E_INIT_GDBUS_METHOD_ASYNC_STRING__STRV(_iface_struct, _dbus_sig_name_str, _sig_name_var, _method_sig_id, _done_sig_id) \
	E_INIT_GDBUS_METHOD_CALL_STRING (_iface_struct, _dbus_sig_name_str, _sig_name_var, _method_sig_id) \
	E_INIT_GDBUS_METHOD_DONE_STRV   (_iface_struct, _sig_name_var, _done_sig_id)

#define E_INIT_GDBUS_METHOD_ASYNC_STRV__STRING(_iface_struct, _dbus_sig_name_str, _sig_name_var, _method_sig_id, _done_sig_id) \
	E_INIT_GDBUS_METHOD_CALL_STRV (_iface_struct, _dbus_sig_name_str, _sig_name_var, _method_sig_id) \
	E_INIT_GDBUS_METHOD_DONE_STRING (_iface_struct, _sig_name_var, _done_sig_id)

#define E_INIT_GDBUS_METHOD_ASYNC_STRV__STRV(_iface_struct, _dbus_sig_name_str, _sig_name_var, _method_sig_id, _done_sig_id) \
	E_INIT_GDBUS_METHOD_CALL_STRV (_iface_struct, _dbus_sig_name_str, _sig_name_var, _method_sig_id) \
	E_INIT_GDBUS_METHOD_DONE_STRV (_iface_struct, _sig_name_var, _done_sig_id)

#define E_DECLARE_GDBUS_SIGNAL_EMISSION_HOOK_VOID(_iface_name, _sig_name) \
	static gboolean \
	signal_emission_hook_cb_ ## _sig_name (GSignalInvocationHint *ihint, guint n_param_values, const GValue *param_values, gpointer user_data) \
	{ \
		return e_gdbus_signal_emission_hook_void (ihint, n_param_values, param_values, user_data, _iface_name); \
	}

#define E_DECLARE_GDBUS_SIGNAL_EMISSION_HOOK_BOOLEAN(_iface_name, _sig_name) \
	static gboolean \
	signal_emission_hook_cb_ ## _sig_name (GSignalInvocationHint *ihint, guint n_param_values, const GValue *param_values, gpointer user_data) \
	{ \
		return e_gdbus_signal_emission_hook_boolean (ihint, n_param_values, param_values, user_data, _iface_name); \
	}

#define E_DECLARE_GDBUS_SIGNAL_EMISSION_HOOK_STRING(_iface_name, _sig_name) \
	static gboolean \
	signal_emission_hook_cb_ ## _sig_name (GSignalInvocationHint *ihint, guint n_param_values, const GValue *param_values, gpointer user_data) \
	{ \
		return e_gdbus_signal_emission_hook_string (ihint, n_param_values, param_values, user_data, _iface_name); \
	}

#define E_DECLARE_GDBUS_SIGNAL_EMISSION_HOOK_STRV(_iface_name, _sig_name) \
	static gboolean \
	signal_emission_hook_cb_ ## _sig_name (GSignalInvocationHint *ihint, guint n_param_values, const GValue *param_values, gpointer user_data) \
	{ \
		return e_gdbus_signal_emission_hook_strv (ihint, n_param_values, param_values, user_data, _iface_name); \
	}

#define E_DECLARE_GDBUS_SIGNAL_EMISSION_HOOK_UINT(_iface_name, _sig_name) \
	static gboolean \
	signal_emission_hook_cb_ ## _sig_name (GSignalInvocationHint *ihint, guint n_param_values, const GValue *param_values, gpointer user_data) \
	{ \
		return e_gdbus_signal_emission_hook_uint (ihint, n_param_values, param_values, user_data, _iface_name); \
	}

#define E_DECLARE_GDBUS_SIGNAL_EMISSION_HOOK_UINT_STRING(_iface_name, _sig_name) \
	static gboolean \
	signal_emission_hook_cb_ ## _sig_name (GSignalInvocationHint *ihint, guint n_param_values, const GValue *param_values, gpointer user_data) \
	{ \
		return e_gdbus_signal_emission_hook_uint_string (ihint, n_param_values, param_values, user_data, _iface_name); \
	}

#define E_DECLARE_GDBUS_SIGNAL_EMISSION_HOOK_ASYNC_VOID(_iface_name, _sig_name) \
	static gboolean \
	signal_emission_hook_cb_ ## _sig_name (GSignalInvocationHint *ihint, guint n_param_values, const GValue *param_values, gpointer user_data) \
	{ \
		return e_gdbus_signal_emission_hook_async_void (ihint, n_param_values, param_values, user_data, _iface_name); \
	}

#define E_DECLARE_GDBUS_SIGNAL_EMISSION_HOOK_ASYNC_BOOLEAN(_iface_name, _sig_name) \
	static gboolean \
	signal_emission_hook_cb_ ## _sig_name (GSignalInvocationHint *ihint, guint n_param_values, const GValue *param_values, gpointer user_data) \
	{ \
		return e_gdbus_signal_emission_hook_async_boolean (ihint, n_param_values, param_values, user_data, _iface_name); \
	}

#define E_DECLARE_GDBUS_SIGNAL_EMISSION_HOOK_ASYNC_STRING(_iface_name, _sig_name) \
	static gboolean \
	signal_emission_hook_cb_ ## _sig_name (GSignalInvocationHint *ihint, guint n_param_values, const GValue *param_values, gpointer user_data) \
	{ \
		return e_gdbus_signal_emission_hook_async_string (ihint, n_param_values, param_values, user_data, _iface_name); \
	}

#define E_DECLARE_GDBUS_SIGNAL_EMISSION_HOOK_ASYNC_STRV(_iface_name, _sig_name) \
	static gboolean \
	signal_emission_hook_cb_ ## _sig_name (GSignalInvocationHint *ihint, guint n_param_values, const GValue *param_values, gpointer user_data) \
	{ \
		return e_gdbus_signal_emission_hook_async_strv (ihint, n_param_values, param_values, user_data, _iface_name); \
	}

#define E_DECLARE_GDBUS_SIGNAL_EMISSION_HOOK_ASYNC_UINT(_iface_name, _sig_name) \
	static gboolean \
	signal_emission_hook_cb_ ## _sig_name (GSignalInvocationHint *ihint, guint n_param_values, const GValue *param_values, gpointer user_data) \
	{ \
		return e_gdbus_signal_emission_hook_async_uint (ihint, n_param_values, param_values, user_data, _iface_name); \
	}

#define E_DECLARE_GDBUS_METHOD_DONE_EMISSION_HOOK_ASYNC_VOID(_iface_name, _sig_name) \
	E_DECLARE_GDBUS_SIGNAL_EMISSION_HOOK_ASYNC_VOID (_iface_name, _sig_name ## _done)

#define E_DECLARE_GDBUS_METHOD_DONE_EMISSION_HOOK_ASYNC_BOOLEAN(_iface_name, _sig_name) \
	E_DECLARE_GDBUS_SIGNAL_EMISSION_HOOK_ASYNC_BOOLEAN (_iface_name, _sig_name ## _done)

#define E_DECLARE_GDBUS_METHOD_DONE_EMISSION_HOOK_ASYNC_STRING(_iface_name, _sig_name) \
	E_DECLARE_GDBUS_SIGNAL_EMISSION_HOOK_ASYNC_STRING (_iface_name, _sig_name ## _done)

#define E_DECLARE_GDBUS_METHOD_DONE_EMISSION_HOOK_ASYNC_STRV(_iface_name, _sig_name) \
	E_DECLARE_GDBUS_SIGNAL_EMISSION_HOOK_ASYNC_STRV (_iface_name, _sig_name ## _done)

#define E_DECLARE_GDBUS_METHOD_DONE_EMISSION_HOOK_ASYNC_UINT(_iface_name, _sig_name) \
	E_DECLARE_GDBUS_SIGNAL_EMISSION_HOOK_ASYNC_UINT (_iface_name, _sig_name ## _done)

#define E_GDBUS_CONNECT_METHOD_DONE_SIGNAL_VOID(_sig_name) \
	g_signal_connect (proxy, # _sig_name "_done", G_CALLBACK (e_gdbus_proxy_async_method_done_void), NULL);

#define E_GDBUS_CONNECT_METHOD_DONE_SIGNAL_BOOLEAN(_sig_name) \
	g_signal_connect (proxy, # _sig_name "_done", G_CALLBACK (e_gdbus_proxy_async_method_done_boolean), NULL);

#define E_GDBUS_CONNECT_METHOD_DONE_SIGNAL_STRING(_sig_name) \
	g_signal_connect (proxy, # _sig_name "_done", G_CALLBACK (e_gdbus_proxy_async_method_done_string), NULL);

#define E_GDBUS_CONNECT_METHOD_DONE_SIGNAL_STRV(_sig_name) \
	g_signal_connect (proxy, # _sig_name "_done", G_CALLBACK (e_gdbus_proxy_async_method_done_strv), NULL);

#define E_GDBUS_CONNECT_METHOD_DONE_SIGNAL_UINT(_sig_name) \
	g_signal_connect (proxy, # _sig_name "_done", G_CALLBACK (e_gdbus_proxy_async_method_done_uint), NULL);

void e_gdbus_proxy_emit_signal (GDBusProxy *proxy, GVariant *parameters, guint signal_id, guint signal_type);
void e_gdbus_stub_handle_method_call (GObject *stub_object, GDBusMethodInvocation *invocation, GVariant *parameters, const gchar *method_name, guint method_id, guint method_type);

gboolean e_gdbus_signal_emission_hook_void          (GSignalInvocationHint *ihint, guint n_param_values, const GValue *param_values, const gchar *signal_name, const gchar *iface_name);
gboolean e_gdbus_signal_emission_hook_boolean       (GSignalInvocationHint *ihint, guint n_param_values, const GValue *param_values, const gchar *signal_name, const gchar *iface_name);
gboolean e_gdbus_signal_emission_hook_string        (GSignalInvocationHint *ihint, guint n_param_values, const GValue *param_values, const gchar *signal_name, const gchar *iface_name);
gboolean e_gdbus_signal_emission_hook_strv          (GSignalInvocationHint *ihint, guint n_param_values, const GValue *param_values, const gchar *signal_name, const gchar *iface_name);
gboolean e_gdbus_signal_emission_hook_uint          (GSignalInvocationHint *ihint, guint n_param_values, const GValue *param_values, const gchar *signal_name, const gchar *iface_name);
gboolean e_gdbus_signal_emission_hook_uint_string   (GSignalInvocationHint *ihint, guint n_param_values, const GValue *param_values, const gchar *signal_name, const gchar *iface_name);

gboolean e_gdbus_signal_emission_hook_async_void    (GSignalInvocationHint *ihint, guint n_param_values, const GValue *param_values, const gchar *signal_name, const gchar *iface_name);
gboolean e_gdbus_signal_emission_hook_async_boolean (GSignalInvocationHint *ihint, guint n_param_values, const GValue *param_values, const gchar *signal_name, const gchar *iface_name);
gboolean e_gdbus_signal_emission_hook_async_string  (GSignalInvocationHint *ihint, guint n_param_values, const GValue *param_values, const gchar *signal_name, const gchar *iface_name);
gboolean e_gdbus_signal_emission_hook_async_strv    (GSignalInvocationHint *ihint, guint n_param_values, const GValue *param_values, const gchar *signal_name, const gchar *iface_name);
gboolean e_gdbus_signal_emission_hook_async_uint    (GSignalInvocationHint *ihint, guint n_param_values, const GValue *param_values, const gchar *signal_name, const gchar *iface_name);

/* GDBus calls method completion functions; after the 'async' method is expected associated done signal */
void e_gdbus_complete_async_method (gpointer object, GDBusMethodInvocation *invocation, guint opid);
void e_gdbus_complete_sync_method_void		(gpointer object, GDBusMethodInvocation *invocation, const GError *error);
void e_gdbus_complete_sync_method_boolean	(gpointer object, GDBusMethodInvocation *invocation, gboolean out_boolean, const GError *error);
void e_gdbus_complete_sync_method_string	(gpointer object, GDBusMethodInvocation *invocation, const gchar *out_string, const GError *error);
void e_gdbus_complete_sync_method_strv		(gpointer object, GDBusMethodInvocation *invocation, const gchar * const *out_strv, const GError *error);
void e_gdbus_complete_sync_method_uint		(gpointer object, GDBusMethodInvocation *invocation, guint out_uint, const GError *error);

/* callbacks on done signal handlers in the client proxy, which implements EGdbusAsyncOpKeeper interface;
 * functions take ownership of out parameters and are responsible for their freeing */
void e_gdbus_proxy_async_method_done_void	(EGdbusAsyncOpKeeper *proxy, guint arg_opid, const GError *error);
void e_gdbus_proxy_async_method_done_boolean	(EGdbusAsyncOpKeeper *proxy, guint arg_opid, const GError *error, gboolean out_boolean);
void e_gdbus_proxy_async_method_done_string	(EGdbusAsyncOpKeeper *proxy, guint arg_opid, const GError *error, const gchar *out_string);
void e_gdbus_proxy_async_method_done_strv	(EGdbusAsyncOpKeeper *proxy, guint arg_opid, const GError *error, const gchar * const *out_strv);
void e_gdbus_proxy_async_method_done_uint	(EGdbusAsyncOpKeeper *proxy, guint arg_opid, const GError *error, guint out_uint);

void e_gdbus_proxy_call_void	(const gchar *method_name, gpointer source_tag, EGdbusAsyncOpKeeper *proxy, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
void e_gdbus_proxy_call_boolean	(const gchar *method_name, gpointer source_tag, EGdbusAsyncOpKeeper *proxy, gboolean in_boolean, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
void e_gdbus_proxy_call_string	(const gchar *method_name, gpointer source_tag, EGdbusAsyncOpKeeper *proxy, const gchar *in_string, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
void e_gdbus_proxy_call_strv	(const gchar *method_name, gpointer source_tag, EGdbusAsyncOpKeeper *proxy, const gchar * const *in_strv, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
void e_gdbus_proxy_call_uint	(const gchar *method_name, gpointer source_tag, EGdbusAsyncOpKeeper *proxy, guint in_uint, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);

gboolean e_gdbus_proxy_finish_call_void    (EGdbusAsyncOpKeeper *proxy, GAsyncResult *result, GError **error, gpointer source_tag);
gboolean e_gdbus_proxy_finish_call_boolean (EGdbusAsyncOpKeeper *proxy, GAsyncResult *result, gboolean *out_boolean, GError **error, gpointer source_tag);
gboolean e_gdbus_proxy_finish_call_string  (EGdbusAsyncOpKeeper *proxy, GAsyncResult *result, gchar **out_string, GError **error, gpointer source_tag);
gboolean e_gdbus_proxy_finish_call_strv    (EGdbusAsyncOpKeeper *proxy, GAsyncResult *result, gchar ***out_strv, GError **error, gpointer source_tag);
gboolean e_gdbus_proxy_finish_call_uint    (EGdbusAsyncOpKeeper *proxy, GAsyncResult *result, guint *out_uint, GError **error, gpointer source_tag);

typedef void (* EGdbusCallStartVoid)	(GDBusProxy *proxy, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
typedef void (* EGdbusCallStartBoolean)	(GDBusProxy *proxy, gboolean in_boolean, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
typedef void (* EGdbusCallStartString)	(GDBusProxy *proxy, const gchar *in_string, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
typedef void (* EGdbusCallStartStrv)	(GDBusProxy *proxy, const gchar * const *in_strv, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
typedef void (* EGdbusCallStartUint)	(GDBusProxy *proxy, guint in_uint, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);

typedef gboolean (* EGdbusCallFinishVoid)	(GDBusProxy *proxy, GAsyncResult *result, GError **error);
typedef gboolean (* EGdbusCallFinishBoolean)	(GDBusProxy *proxy, GAsyncResult *result, gboolean *out_boolean, GError **error);
typedef gboolean (* EGdbusCallFinishString)	(GDBusProxy *proxy, GAsyncResult *result, gchar **out_string, GError **error);
typedef gboolean (* EGdbusCallFinishStrv)	(GDBusProxy *proxy, GAsyncResult *result, gchar ***out_strv, GError **error);
typedef gboolean (* EGdbusCallFinishUint)	(GDBusProxy *proxy, GAsyncResult *result, guint *out_uint, GError **error);

/* this is for methods which are dividied into invocation and done signal */
gboolean e_gdbus_proxy_call_sync_void__void	(GDBusProxy *proxy, GCancellable *cancellable, GError **error, EGdbusCallStartVoid start_func, EGdbusCallFinishVoid finish_func);
gboolean e_gdbus_proxy_call_sync_void__boolean	(GDBusProxy *proxy, gboolean *out_boolean, GCancellable *cancellable, GError **error, EGdbusCallStartVoid start_func, EGdbusCallFinishBoolean finish_func);
gboolean e_gdbus_proxy_call_sync_void__string	(GDBusProxy *proxy, gchar **out_string, GCancellable *cancellable, GError **error, EGdbusCallStartVoid start_func, EGdbusCallFinishString finish_func);
gboolean e_gdbus_proxy_call_sync_void__strv	(GDBusProxy *proxy, gchar ***out_strv, GCancellable *cancellable, GError **error, EGdbusCallStartVoid start_func, EGdbusCallFinishStrv finish_func);
gboolean e_gdbus_proxy_call_sync_void__uint	(GDBusProxy *proxy, guint *out_uint, GCancellable *cancellable, GError **error, EGdbusCallStartVoid start_func, EGdbusCallFinishUint finish_func);
gboolean e_gdbus_proxy_call_sync_boolean__void	(GDBusProxy *proxy, gboolean in_boolean, GCancellable *cancellable, GError **error, EGdbusCallStartBoolean start_func, EGdbusCallFinishVoid finish_func);
gboolean e_gdbus_proxy_call_sync_string__void	(GDBusProxy *proxy, const gchar *in_string, GCancellable *cancellable, GError **error, EGdbusCallStartString start_func, EGdbusCallFinishVoid finish_func);
gboolean e_gdbus_proxy_call_sync_strv__void	(GDBusProxy *proxy, const gchar * const *in_strv, GCancellable *cancellable, GError **error, EGdbusCallStartStrv start_func, EGdbusCallFinishVoid finish_func);
gboolean e_gdbus_proxy_call_sync_uint__void	(GDBusProxy *proxy, guint in_uint, GCancellable *cancellable, GError **error, EGdbusCallStartUint start_func, EGdbusCallFinishVoid finish_func);
gboolean e_gdbus_proxy_call_sync_string__string	(GDBusProxy *proxy, const gchar *in_string, gchar **out_string, GCancellable *cancellable, GError **error, EGdbusCallStartString start_func, EGdbusCallFinishString finish_func);
gboolean e_gdbus_proxy_call_sync_string__strv	(GDBusProxy *proxy, const gchar *in_string, gchar ***out_strv, GCancellable *cancellable, GError **error, EGdbusCallStartString start_func, EGdbusCallFinishStrv finish_func);
gboolean e_gdbus_proxy_call_sync_strv__string	(GDBusProxy *proxy, const gchar * const *in_strv, gchar **out_string, GCancellable *cancellable, GError **error, EGdbusCallStartStrv start_func, EGdbusCallFinishString finish_func);
gboolean e_gdbus_proxy_call_sync_strv__strv	(GDBusProxy *proxy, const gchar * const *in_strv, gchar ***out_strv, GCancellable *cancellable, GError **error, EGdbusCallStartStrv start_func, EGdbusCallFinishStrv finish_func);

/* this is for "synchronous" methods, those without done signal */
void	 e_gdbus_proxy_method_call_void			(const gchar *method_name, GDBusProxy *proxy, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
void	 e_gdbus_proxy_method_call_boolean		(const gchar *method_name, GDBusProxy *proxy, gboolean in_boolean, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
void	 e_gdbus_proxy_method_call_string		(const gchar *method_name, GDBusProxy *proxy, const gchar *in_string, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
void	 e_gdbus_proxy_method_call_strv			(const gchar *method_name, GDBusProxy *proxy, const gchar * const *in_strv, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
void	 e_gdbus_proxy_method_call_uint			(const gchar *method_name, GDBusProxy *proxy, guint in_uint, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);

gboolean e_gdbus_proxy_method_call_finish_void		(GDBusProxy *proxy, GAsyncResult *result, GError **error);
gboolean e_gdbus_proxy_method_call_finish_boolean	(GDBusProxy *proxy, GAsyncResult *result, gboolean *out_boolean, GError **error);
gboolean e_gdbus_proxy_method_call_finish_string	(GDBusProxy *proxy, GAsyncResult *result, gchar **out_string, GError **error);
gboolean e_gdbus_proxy_method_call_finish_strv		(GDBusProxy *proxy, GAsyncResult *result, gchar ***out_strv, GError **error);
gboolean e_gdbus_proxy_method_call_finish_uint		(GDBusProxy *proxy, GAsyncResult *result, guint *out_uint, GError **error);

gboolean e_gdbus_proxy_method_call_sync_void__void	(const gchar *method_name, GDBusProxy *proxy, GCancellable *cancellable, GError **error);
gboolean e_gdbus_proxy_method_call_sync_boolean__void	(const gchar *method_name, GDBusProxy *proxy, gboolean in_boolean, GCancellable *cancellable, GError **error);
gboolean e_gdbus_proxy_method_call_sync_string__void	(const gchar *method_name, GDBusProxy *proxy, const gchar *in_string, GCancellable *cancellable, GError **error);
gboolean e_gdbus_proxy_method_call_sync_strv__void	(const gchar *method_name, GDBusProxy *proxy, const gchar * const *in_strv, GCancellable *cancellable, GError **error);
gboolean e_gdbus_proxy_method_call_sync_uint__void	(const gchar *method_name, GDBusProxy *proxy, guint in_uint, GCancellable *cancellable, GError **error);
gboolean e_gdbus_proxy_method_call_sync_string__string	(const gchar *method_name, GDBusProxy *proxy, const gchar *in_string, gchar **out_string, GCancellable *cancellable, GError **error);
gboolean e_gdbus_proxy_method_call_sync_strv__string	(const gchar *method_name, GDBusProxy *proxy, const gchar * const *in_strv, gchar **out_string, GCancellable *cancellable, GError **error);

gchar ** e_gdbus_templates_encode_error	(const GError *in_error);
gboolean e_gdbus_templates_decode_error	(const gchar * const *in_strv, GError **out_error);

gchar ** e_gdbus_templates_encode_two_strings (const gchar *in_str1, const gchar *in_str2);
gboolean e_gdbus_templates_decode_two_strings (const gchar * const *in_strv, gchar **out_str1, gchar **out_str2);

G_END_DECLS

#endif /* E_GDBUS_TEMPLATES_H */

#endif /* __GI_SCANNER__ */

#endif /* EDS_DISABLE_DEPRECATED */

