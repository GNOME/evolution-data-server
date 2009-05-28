/* DO NOT EDIT: automatically built by dist/s_include. */
#ifndef	_qam_ext_h_
#define	_qam_ext_h_

#if defined(__cplusplus)
extern "C" {
#endif

gint __qam_position __P((DBC *, db_recno_t *, qam_position_mode, gint *));
gint __qam_pitem __P((DBC *,  QPAGE *, u_int32_t, db_recno_t, DBT *));
gint __qam_append __P((DBC *, DBT *, DBT *));
gint __qam_c_dup __P((DBC *, DBC *));
gint __qam_c_init __P((DBC *));
gint __qam_truncate __P((DB *, DB_TXN *, u_int32_t *));
gint __qam_incfirst_log __P((DB *, DB_TXN *, DB_LSN *, u_int32_t, db_recno_t, db_pgno_t));
gint __qam_incfirst_getpgnos __P((DB_ENV *, DBT *, DB_LSN *, db_recops, gpointer ));
gint __qam_incfirst_print __P((DB_ENV *, DBT *, DB_LSN *, db_recops, gpointer ));
gint __qam_incfirst_read __P((DB_ENV *, gpointer , __qam_incfirst_args **));
gint __qam_mvptr_log __P((DB *, DB_TXN *, DB_LSN *, u_int32_t, u_int32_t, db_recno_t, db_recno_t, db_recno_t, db_recno_t, DB_LSN *, db_pgno_t));
gint __qam_mvptr_getpgnos __P((DB_ENV *, DBT *, DB_LSN *, db_recops, gpointer ));
gint __qam_mvptr_print __P((DB_ENV *, DBT *, DB_LSN *, db_recops, gpointer ));
gint __qam_mvptr_read __P((DB_ENV *, gpointer , __qam_mvptr_args **));
gint __qam_del_log __P((DB *, DB_TXN *, DB_LSN *, u_int32_t, DB_LSN *, db_pgno_t, u_int32_t, db_recno_t));
gint __qam_del_getpgnos __P((DB_ENV *, DBT *, DB_LSN *, db_recops, gpointer ));
gint __qam_del_print __P((DB_ENV *, DBT *, DB_LSN *, db_recops, gpointer ));
gint __qam_del_read __P((DB_ENV *, gpointer , __qam_del_args **));
gint __qam_add_log __P((DB *, DB_TXN *, DB_LSN *, u_int32_t, DB_LSN *, db_pgno_t, u_int32_t, db_recno_t, const DBT *, u_int32_t, const DBT *));
gint __qam_add_getpgnos __P((DB_ENV *, DBT *, DB_LSN *, db_recops, gpointer ));
gint __qam_add_print __P((DB_ENV *, DBT *, DB_LSN *, db_recops, gpointer ));
gint __qam_add_read __P((DB_ENV *, gpointer , __qam_add_args **));
gint __qam_delext_log __P((DB *, DB_TXN *, DB_LSN *, u_int32_t, DB_LSN *, db_pgno_t, u_int32_t, db_recno_t, const DBT *));
gint __qam_delext_getpgnos __P((DB_ENV *, DBT *, DB_LSN *, db_recops, gpointer ));
gint __qam_delext_print __P((DB_ENV *, DBT *, DB_LSN *, db_recops, gpointer ));
gint __qam_delext_read __P((DB_ENV *, gpointer , __qam_delext_args **));
gint __qam_init_print __P((DB_ENV *, gint (***)(DB_ENV *, DBT *, DB_LSN *, db_recops, gpointer ), size_t *));
gint __qam_init_getpgnos __P((DB_ENV *, gint (***)(DB_ENV *, DBT *, DB_LSN *, db_recops, gpointer ), size_t *));
gint __qam_init_recover __P((DB_ENV *, gint (***)(DB_ENV *, DBT *, DB_LSN *, db_recops, gpointer ), size_t *));
gint __qam_mswap __P((PAGE *));
gint __qam_pgin_out __P((DB_ENV *, db_pgno_t, gpointer , DBT *));
gint __qam_fprobe __P((DB *, db_pgno_t, gpointer , qam_probe_mode, u_int32_t));
gint __qam_fclose __P((DB *, db_pgno_t));
gint __qam_fremove __P((DB *, db_pgno_t));
gint __qam_sync __P((DB *, u_int32_t));
gint __qam_gen_filelist __P(( DB *, QUEUE_FILELIST **));
gint __qam_extent_names __P((DB_ENV *, gchar *, gchar ***));
void __qam_exid __P((DB *, u_int8_t *, u_int32_t));
gint __qam_db_create __P((DB *));
gint __qam_db_close __P((DB *));
gint __db_prqueue __P((DB *, FILE *, u_int32_t));
gint __qam_remove __P((DB *, DB_TXN *, const gchar *, const gchar *, DB_LSN *));
gint __qam_rename __P((DB *, DB_TXN *, const gchar *, const gchar *, const gchar *));
gint __qam_open __P((DB *, DB_TXN *, const gchar *, db_pgno_t, int, u_int32_t));
gint __qam_metachk __P((DB *, const gchar *, QMETA *));
gint __qam_new_file __P((DB *, DB_TXN *, DB_FH *, const gchar *));
gint __qam_incfirst_recover __P((DB_ENV *, DBT *, DB_LSN *, db_recops, gpointer ));
gint __qam_mvptr_recover __P((DB_ENV *, DBT *, DB_LSN *, db_recops, gpointer ));
gint __qam_del_recover __P((DB_ENV *, DBT *, DB_LSN *, db_recops, gpointer ));
gint __qam_delext_recover __P((DB_ENV *, DBT *, DB_LSN *, db_recops, gpointer ));
gint __qam_add_recover __P((DB_ENV *, DBT *, DB_LSN *, db_recops, gpointer ));
gint __qam_stat __P((DB *, gpointer , u_int32_t));
gint __qam_31_qammeta __P((DB *, gchar *, u_int8_t *));
gint __qam_32_qammeta __P((DB *, gchar *, u_int8_t *));
gint __qam_vrfy_meta __P((DB *, VRFY_DBINFO *, QMETA *, db_pgno_t, u_int32_t));
gint __qam_vrfy_data __P((DB *, VRFY_DBINFO *, QPAGE *, db_pgno_t, u_int32_t));
gint __qam_vrfy_structure __P((DB *, VRFY_DBINFO *, u_int32_t));

#if defined(__cplusplus)
}
#endif
#endif /* !_qam_ext_h_ */
