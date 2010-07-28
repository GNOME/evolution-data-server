
#ifndef ___e_gdbus_gdbus_cclosure_marshaller_MARSHAL_H__
#define ___e_gdbus_gdbus_cclosure_marshaller_MARSHAL_H__

#include	<glib-object.h>

G_BEGIN_DECLS

/* BOOLEAN:OBJECT,STRING,UINT (e-gdbus-marshallers.list:1) */
extern void _e_gdbus_gdbus_cclosure_marshaller_BOOLEAN__OBJECT_STRING_UINT (GClosure     *closure,
                                                                            GValue       *return_value,
                                                                            guint         n_param_values,
                                                                            const GValue *param_values,
                                                                            gpointer      invocation_hint,
                                                                            gpointer      marshal_data);

/* VOID:BOXED (e-gdbus-marshallers.list:2) */
#define _e_gdbus_gdbus_cclosure_marshaller_VOID__BOXED	g_cclosure_marshal_VOID__BOXED

/* VOID:STRING,UINT (e-gdbus-marshallers.list:3) */
extern void _e_gdbus_gdbus_cclosure_marshaller_VOID__STRING_UINT (GClosure     *closure,
                                                                  GValue       *return_value,
                                                                  guint         n_param_values,
                                                                  const GValue *param_values,
                                                                  gpointer      invocation_hint,
                                                                  gpointer      marshal_data);

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

/* VOID:STRING (e-gdbus-marshallers.list:6) */
#define _e_gdbus_gdbus_cclosure_marshaller_VOID__STRING	g_cclosure_marshal_VOID__STRING

/* VOID:BOOLEAN (e-gdbus-marshallers.list:7) */
#define _e_gdbus_gdbus_cclosure_marshaller_VOID__BOOLEAN	g_cclosure_marshal_VOID__BOOLEAN

/* VOID:INT (e-gdbus-marshallers.list:8) */
#define _e_gdbus_gdbus_cclosure_marshaller_VOID__INT	g_cclosure_marshal_VOID__INT

/* BOOLEAN:OBJECT,BOOLEAN,STRING,STRING (e-gdbus-marshallers.list:9) */
extern void _e_gdbus_gdbus_cclosure_marshaller_BOOLEAN__OBJECT_BOOLEAN_STRING_STRING (GClosure     *closure,
                                                                                      GValue       *return_value,
                                                                                      guint         n_param_values,
                                                                                      const GValue *param_values,
                                                                                      gpointer      invocation_hint,
                                                                                      gpointer      marshal_data);

/* BOOLEAN:OBJECT,UINT (e-gdbus-marshallers.list:10) */
extern void _e_gdbus_gdbus_cclosure_marshaller_BOOLEAN__OBJECT_UINT (GClosure     *closure,
                                                                     GValue       *return_value,
                                                                     guint         n_param_values,
                                                                     const GValue *param_values,
                                                                     gpointer      invocation_hint,
                                                                     gpointer      marshal_data);

/* BOOLEAN:OBJECT,STRING,STRING (e-gdbus-marshallers.list:11) */
extern void _e_gdbus_gdbus_cclosure_marshaller_BOOLEAN__OBJECT_STRING_STRING (GClosure     *closure,
                                                                              GValue       *return_value,
                                                                              guint         n_param_values,
                                                                              const GValue *param_values,
                                                                              gpointer      invocation_hint,
                                                                              gpointer      marshal_data);

/* BOOLEAN:OBJECT,STRING (e-gdbus-marshallers.list:12) */
extern void _e_gdbus_gdbus_cclosure_marshaller_BOOLEAN__OBJECT_STRING (GClosure     *closure,
                                                                       GValue       *return_value,
                                                                       guint         n_param_values,
                                                                       const GValue *param_values,
                                                                       gpointer      invocation_hint,
                                                                       gpointer      marshal_data);

/* BOOLEAN:OBJECT,BOXED,UINT,UINT (e-gdbus-marshallers.list:13) */
extern void _e_gdbus_gdbus_cclosure_marshaller_BOOLEAN__OBJECT_BOXED_UINT_UINT (GClosure     *closure,
                                                                                GValue       *return_value,
                                                                                guint         n_param_values,
                                                                                const GValue *param_values,
                                                                                gpointer      invocation_hint,
                                                                                gpointer      marshal_data);

/* BOOLEAN:OBJECT,STRING,STRING,UINT (e-gdbus-marshallers.list:14) */
extern void _e_gdbus_gdbus_cclosure_marshaller_BOOLEAN__OBJECT_STRING_STRING_UINT (GClosure     *closure,
                                                                                   GValue       *return_value,
                                                                                   guint         n_param_values,
                                                                                   const GValue *param_values,
                                                                                   gpointer      invocation_hint,
                                                                                   gpointer      marshal_data);

G_END_DECLS

#endif /* ___e_gdbus_gdbus_cclosure_marshaller_MARSHAL_H__ */

