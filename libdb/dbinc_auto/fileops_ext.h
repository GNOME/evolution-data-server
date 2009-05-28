/* DO NOT EDIT: automatically built by dist/s_include. */
#ifndef	_fileops_ext_h_
#define	_fileops_ext_h_

#if defined(__cplusplus)
extern "C" {
#endif

gint __fop_create_log __P((DB_ENV *, DB_TXN *, DB_LSN *, u_int32_t, const DBT *, u_int32_t, u_int32_t));
gint __fop_create_getpgnos __P((DB_ENV *, DBT *, DB_LSN *, db_recops, gpointer ));
gint __fop_create_print __P((DB_ENV *, DBT *, DB_LSN *, db_recops, gpointer ));
gint __fop_create_read __P((DB_ENV *, gpointer , __fop_create_args **));
gint __fop_remove_log __P((DB_ENV *, DB_TXN *, DB_LSN *, u_int32_t, const DBT *, const DBT *, u_int32_t));
gint __fop_remove_getpgnos __P((DB_ENV *, DBT *, DB_LSN *, db_recops, gpointer ));
gint __fop_remove_print __P((DB_ENV *, DBT *, DB_LSN *, db_recops, gpointer ));
gint __fop_remove_read __P((DB_ENV *, gpointer , __fop_remove_args **));
gint __fop_write_log __P((DB_ENV *, DB_TXN *, DB_LSN *, u_int32_t, const DBT *, u_int32_t, u_int32_t, const DBT *, u_int32_t));
gint __fop_write_getpgnos __P((DB_ENV *, DBT *, DB_LSN *, db_recops, gpointer ));
gint __fop_write_print __P((DB_ENV *, DBT *, DB_LSN *, db_recops, gpointer ));
gint __fop_write_read __P((DB_ENV *, gpointer , __fop_write_args **));
gint __fop_rename_log __P((DB_ENV *, DB_TXN *, DB_LSN *, u_int32_t, const DBT *, const DBT *, const DBT *, u_int32_t));
gint __fop_rename_getpgnos __P((DB_ENV *, DBT *, DB_LSN *, db_recops, gpointer ));
gint __fop_rename_print __P((DB_ENV *, DBT *, DB_LSN *, db_recops, gpointer ));
gint __fop_rename_read __P((DB_ENV *, gpointer , __fop_rename_args **));
gint __fop_file_remove_log __P((DB_ENV *, DB_TXN *, DB_LSN *, u_int32_t, const DBT *, const DBT *, const DBT *, u_int32_t, u_int32_t));
gint __fop_file_remove_getpgnos __P((DB_ENV *, DBT *, DB_LSN *, db_recops, gpointer ));
gint __fop_file_remove_print __P((DB_ENV *, DBT *, DB_LSN *, db_recops, gpointer ));
gint __fop_file_remove_read __P((DB_ENV *, gpointer , __fop_file_remove_args **));
gint __fop_init_print __P((DB_ENV *, gint (***)(DB_ENV *, DBT *, DB_LSN *, db_recops, gpointer ), size_t *));
gint __fop_init_getpgnos __P((DB_ENV *, gint (***)(DB_ENV *, DBT *, DB_LSN *, db_recops, gpointer ), size_t *));
gint __fop_init_recover __P((DB_ENV *, gint (***)(DB_ENV *, DBT *, DB_LSN *, db_recops, gpointer ), size_t *));
gint __fop_create __P((DB_ENV *, DB_TXN *, DB_FH *, const gchar *, APPNAME, int));
gint __fop_remove __P((DB_ENV *, DB_TXN *, u_int8_t *, const gchar *, APPNAME));
gint __fop_write __P((DB_ENV *, DB_TXN *, const gchar *, APPNAME, DB_FH *, u_int32_t, u_int8_t *, u_int32_t, u_int32_t));
gint __fop_rename __P((DB_ENV *, DB_TXN *, const gchar *, const gchar *, u_int8_t *, APPNAME));
gint __fop_create_recover __P((DB_ENV *, DBT *, DB_LSN *, db_recops, gpointer ));
gint __fop_remove_recover __P((DB_ENV *, DBT *, DB_LSN *, db_recops, gpointer ));
gint __fop_write_recover __P((DB_ENV *, DBT *, DB_LSN *, db_recops, gpointer ));
gint __fop_rename_recover __P((DB_ENV *, DBT *, DB_LSN *, db_recops, gpointer ));
gint __fop_file_remove_recover __P((DB_ENV *, DBT *, DB_LSN *, db_recops, gpointer ));
gint __fop_lock_handle __P((DB_ENV *, DB *, u_int32_t, db_lockmode_t, DB_LOCK *, u_int32_t));
gint __fop_file_setup __P((DB *, DB_TXN *, const gchar *, int, u_int32_t, u_int32_t *));
gint __fop_subdb_setup __P((DB *, DB_TXN *, const gchar *, const gchar *, int, u_int32_t));
gint __fop_remove_setup __P((DB *, DB_TXN *, const gchar *, u_int32_t));
gint __fop_read_meta __P((DB_ENV *, const gchar *, u_int8_t *, size_t, DB_FH *, int, size_t *, u_int32_t));
gint __fop_dummy __P((DB *, DB_TXN *, const gchar *, const gchar *, u_int32_t));
gint __fop_dbrename __P((DB *, const gchar *, const gchar *));

#if defined(__cplusplus)
}
#endif
#endif /* !_fileops_ext_h_ */
