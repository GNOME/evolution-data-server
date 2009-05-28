/* DO NOT EDIT: automatically built by dist/s_include. */
#ifndef	_os_ext_h_
#define	_os_ext_h_

#if defined(__cplusplus)
extern "C" {
#endif

gint __os_abspath __P((const gchar *));
gint __os_umalloc __P((DB_ENV *, size_t, gpointer ));
gint __os_urealloc __P((DB_ENV *, size_t, gpointer ));
gint __os_ufree __P((DB_ENV *, gpointer ));
gint __os_strdup __P((DB_ENV *, const gchar *, gpointer ));
gint __os_calloc __P((DB_ENV *, size_t, size_t, gpointer ));
gint __os_malloc __P((DB_ENV *, size_t, gpointer ));
gint __os_realloc __P((DB_ENV *, size_t, gpointer ));
void __os_free __P((DB_ENV *, gpointer ));
gpointer __ua_memcpy __P((gpointer , gconstpointer , size_t));
gint __os_clock __P((DB_ENV *, u_int32_t *, u_int32_t *));
gint __os_fs_notzero __P((void));
gint __os_dirlist __P((DB_ENV *, const gchar *, gchar ***, gint *));
void __os_dirfree __P((DB_ENV *, gchar **, int));
gint __os_get_errno_ret_zero __P((void));
gint __os_get_errno __P((void));
void __os_set_errno __P((int));
gint __os_fileid __P((DB_ENV *, const gchar *, int, u_int8_t *));
gint __os_fsync __P((DB_ENV *, DB_FH *));
gint __os_openhandle __P((DB_ENV *, const gchar *, int, int, DB_FH *));
gint __os_closehandle __P((DB_ENV *, DB_FH *));
void __os_id __P((u_int32_t *));
gint __os_r_sysattach __P((DB_ENV *, REGINFO *, REGION *));
gint __os_r_sysdetach __P((DB_ENV *, REGINFO *, int));
gint __os_mapfile __P((DB_ENV *, gchar *, DB_FH *, size_t, int, gpointer *));
gint __os_unmapfile __P((DB_ENV *, gpointer , size_t));
u_int32_t __db_oflags __P((int));
gint __db_omode __P((const gchar *));
gint __os_open __P((DB_ENV *, const gchar *, u_int32_t, int, DB_FH *));
#ifdef HAVE_QNX
gint __os_shmname __P((DB_ENV *, const gchar *, gchar **));
#endif
gint __os_r_attach __P((DB_ENV *, REGINFO *, REGION *));
gint __os_r_detach __P((DB_ENV *, REGINFO *, int));
gint __os_rename __P((DB_ENV *, const gchar *, const gchar *, u_int32_t));
gint __os_isroot __P((void));
gchar *__db_rpath __P((const gchar *));
gint __os_io __P((DB_ENV *, DB_IO *, int, size_t *));
gint __os_read __P((DB_ENV *, DB_FH *, gpointer , size_t, size_t *));
gint __os_write __P((DB_ENV *, DB_FH *, gpointer , size_t, size_t *));
gint __os_seek __P((DB_ENV *, DB_FH *, size_t, db_pgno_t, u_int32_t, int, DB_OS_SEEK));
gint __os_sleep __P((DB_ENV *, u_long, u_long));
gint __os_spin __P((DB_ENV *));
void __os_yield __P((DB_ENV*, u_long));
gint __os_exists __P((const gchar *, gint *));
gint __os_ioinfo __P((DB_ENV *, const gchar *, DB_FH *, u_int32_t *, u_int32_t *, u_int32_t *));
gint __os_tmpdir __P((DB_ENV *, u_int32_t));
gint __os_region_unlink __P((DB_ENV *, const gchar *));
gint __os_unlink __P((DB_ENV *, const gchar *));
#if defined(DB_WIN32)
gint __os_win32_errno __P((void));
#endif
gint __os_fsync __P((DB_ENV *, DB_FH *));
gint __os_openhandle __P((DB_ENV *, const gchar *, int, int, DB_FH *));
gint __os_closehandle __P((DB_ENV *, DB_FH *));
gint __os_io __P((DB_ENV *, DB_IO *, int, size_t *));
gint __os_read __P((DB_ENV *, DB_FH *, gpointer , size_t, size_t *));
gint __os_write __P((DB_ENV *, DB_FH *, gpointer , size_t, size_t *));
gint __os_exists __P((const gchar *, gint *));
gint __os_ioinfo __P((DB_ENV *, const gchar *, DB_FH *, u_int32_t *, u_int32_t *, u_int32_t *));
gint __os_is_winnt __P((void));

#if defined(__cplusplus)
}
#endif
#endif /* !_os_ext_h_ */
