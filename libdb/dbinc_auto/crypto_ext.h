/* DO NOT EDIT: automatically built by dist/s_include. */
#ifndef	_crypto_ext_h_
#define	_crypto_ext_h_

#if defined(__cplusplus)
extern "C" {
#endif

gint __aes_setup __P((DB_ENV *, DB_CIPHER *));
gint __aes_adj_size __P((size_t));
gint __aes_close __P((DB_ENV *, gpointer ));
gint __aes_decrypt __P((DB_ENV *, gpointer , gpointer , u_int8_t *, size_t));
gint __aes_encrypt __P((DB_ENV *, gpointer , gpointer , u_int8_t *, size_t));
gint __aes_init __P((DB_ENV *, DB_CIPHER *));
gint __crypto_region_init __P((DB_ENV *));
gint __crypto_dbenv_close __P((DB_ENV *));
gint __crypto_algsetup __P((DB_ENV *, DB_CIPHER *, u_int32_t, int));
gint __crypto_decrypt_meta __P((DB_ENV *, DB *, u_int8_t *, int));
gint __db_generate_iv __P((DB_ENV *, u_int32_t *));
gint __db_rijndaelKeySetupEnc __P((u32 *, const u8 *, int));
gint __db_rijndaelKeySetupDec __P((u32 *, const u8 *, int));
void __db_rijndaelEncrypt __P((u32 *, int, const u8 *, u8 *));
void __db_rijndaelDecrypt __P((u32 *, int, const u8 *, u8 *));
void __db_rijndaelEncryptRound __P((const u32 *, int, u8 *, int));
void __db_rijndaelDecryptRound __P((const u32 *, int, u8 *, int));
gint __db_makeKey __P((keyInstance *, int, int, gchar *));
gint __db_cipherInit __P((cipherInstance *, int, gchar *));
gint __db_blockEncrypt __P((cipherInstance *, keyInstance *, BYTE *, size_t, BYTE *));
gint __db_padEncrypt __P((cipherInstance *, keyInstance *, BYTE *, int, BYTE *));
gint __db_blockDecrypt __P((cipherInstance *, keyInstance *, BYTE *, size_t, BYTE *));
gint __db_padDecrypt __P((cipherInstance *, keyInstance *, BYTE *, int, BYTE *));
gint __db_cipherUpdateRounds __P((cipherInstance *, keyInstance *, BYTE *, int, BYTE *, int));

#if defined(__cplusplus)
}
#endif
#endif /* !_crypto_ext_h_ */
