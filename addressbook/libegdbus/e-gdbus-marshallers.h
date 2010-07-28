
#ifndef ___e_gdbus_gdbus_cclosure_marshaller_MARSHAL_H__
#define ___e_gdbus_gdbus_cclosure_marshaller_MARSHAL_H__

#include	<glib-object.h>

G_BEGIN_DECLS

/* BOOLEAN:OBJECT,STRING (e-gdbus-marshallers.list:1) */
extern void _e_gdbus_gdbus_cclosure_marshaller_BOOLEAN__OBJECT_STRING (GClosure     *closure,
                                                                       GValue       *return_value,
                                                                       guint         n_param_values,
                                                                       const GValue *param_values,
                                                                       gpointer      invocation_hint,
                                                                       gpointer      marshal_data);

/* VOID:BOXED (e-gdbus-marshallers.list:2) */
#define _e_gdbus_gdbus_cclosure_marshaller_VOID__BOXED	g_cclosure_marshal_VOID__BOXED

/* VOID:STRING (e-gdbus-marshallers.list:3) */
#define _e_gdbus_gdbus_cclosure_marshaller_VOID__STRING	g_cclosure_marshal_VOID__STRING

/* VOID:UINT,STRING (e-gdbus-marshallers.list:4) */
extern void _e_gdbus_gdbus_cclosure_marshaller_VOID__UINT_STRING (GClosure     *closure,
                                                                  GValue       *return_value,
                                                                  guint         n_param_values,
                                                                  const GValue *param_values,
                                                                  gpointer      invocation_hint,
                                                                  gpointer      marshal_data);

/* BOOLEAN:OBJECT (e-gdbus-marshallers.list:5) */
extern void _e_gdbus_gdbus_cclosure_marshaller_BOOLEAN__OBJECT (GClosure     *closure,
                                                                GValue       *return_value,
                                                                guint         n_param_values,
                                                                const GValue *param_values,
                                                                gpointer      invocation_hint,
                                                                gpointer      marshal_data);

/* VOID:BOOLEAN (e-gdbus-marshallers.list:6) */
#define _e_gdbus_gdbus_cclosure_marshaller_VOID__BOOLEAN	g_cclosure_marshal_VOID__BOOLEAN

/* BOOLEAN:OBJECT,BOOLEAN (e-gdbus-marshallers.list:7) */
extern void _e_gdbus_gdbus_cclosure_marshaller_BOOLEAN__OBJECT_BOOLEAN (GClosure     *closure,
                                                                        GValue       *return_value,
                                                                        guint         n_param_values,
                                                                        const GValue *param_values,
                                                                        gpointer      invocation_hint,
                                                                        gpointer      marshal_data);

/* BOOLEAN:OBJECT,STRING,STRING,STRING (e-gdbus-marshallers.list:8) */
extern void _e_gdbus_gdbus_cclosure_marshaller_BOOLEAN__OBJECT_STRING_STRING_STRING (GClosure     *closure,
                                                                                     GValue       *return_value,
                                                                                     guint         n_param_values,
                                                                                     const GValue *param_values,
                                                                                     gpointer      invocation_hint,
                                                                                     gpointer      marshal_data);

/* BOOLEAN:OBJECT,BOXED (e-gdbus-marshallers.list:9) */
extern void _e_gdbus_gdbus_cclosure_marshaller_BOOLEAN__OBJECT_BOXED (GClosure     *closure,
                                                                      GValue       *return_value,
                                                                      guint         n_param_values,
                                                                      const GValue *param_values,
                                                                      gpointer      invocation_hint,
                                                                      gpointer      marshal_data);

/* BOOLEAN:OBJECT,STRING,UINT (e-gdbus-marshallers.list:10) */
extern void _e_gdbus_gdbus_cclosure_marshaller_BOOLEAN__OBJECT_STRING_UINT (GClosure     *closure,
                                                                            GValue       *return_value,
                                                                            guint         n_param_values,
                                                                            const GValue *param_values,
                                                                            gpointer      invocation_hint,
                                                                            gpointer      marshal_data);

G_END_DECLS

#endif /* ___e_gdbus_gdbus_cclosure_marshaller_MARSHAL_H__ */

