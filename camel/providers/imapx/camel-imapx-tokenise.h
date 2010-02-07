/* ANSI-C code produced by gperf version 3.0.2 */
/* Command-line: gperf -H imap_hash -N imap_tokenise_struct -L ANSI-C -o -t -k'1,$' camel-imapx-tokens.txt  */

#if !((' ' == 32) && ('!' == 33) && ('"' == 34) && ('#' == 35) \
      && ('%' == 37) && ('&' == 38) && ('\'' == 39) && ('(' == 40) \
      && (')' == 41) && ('*' == 42) && ('+' == 43) && (',' == 44) \
      && ('-' == 45) && ('.' == 46) && ('/' == 47) && ('0' == 48) \
      && ('1' == 49) && ('2' == 50) && ('3' == 51) && ('4' == 52) \
      && ('5' == 53) && ('6' == 54) && ('7' == 55) && ('8' == 56) \
      && ('9' == 57) && (':' == 58) && (';' == 59) && ('<' == 60) \
      && ('=' == 61) && ('>' == 62) && ('?' == 63) && ('A' == 65) \
      && ('B' == 66) && ('C' == 67) && ('D' == 68) && ('E' == 69) \
      && ('F' == 70) && ('G' == 71) && ('H' == 72) && ('I' == 73) \
      && ('J' == 74) && ('K' == 75) && ('L' == 76) && ('M' == 77) \
      && ('N' == 78) && ('O' == 79) && ('P' == 80) && ('Q' == 81) \
      && ('R' == 82) && ('S' == 83) && ('T' == 84) && ('U' == 85) \
      && ('V' == 86) && ('W' == 87) && ('X' == 88) && ('Y' == 89) \
      && ('Z' == 90) && ('[' == 91) && ('\\' == 92) && (']' == 93) \
      && ('^' == 94) && ('_' == 95) && ('a' == 97) && ('b' == 98) \
      && ('c' == 99) && ('d' == 100) && ('e' == 101) && ('f' == 102) \
      && ('g' == 103) && ('h' == 104) && ('i' == 105) && ('j' == 106) \
      && ('k' == 107) && ('l' == 108) && ('m' == 109) && ('n' == 110) \
      && ('o' == 111) && ('p' == 112) && ('q' == 113) && ('r' == 114) \
      && ('s' == 115) && ('t' == 116) && ('u' == 117) && ('v' == 118) \
      && ('w' == 119) && ('x' == 120) && ('y' == 121) && ('z' == 122) \
      && ('{' == 123) && ('|' == 124) && ('}' == 125) && ('~' == 126))
/* The character set is not based on ISO-646.  */
#error "gperf generated tables don't work with this execution character set. Please report a bug to <bug-gnu-gperf@gnu.org>."
#endif

#line 3 "camel-imapx-tokens.txt"
struct _imap_keyword {const gchar *name; camel_imapx_id_t id; };

#define TOTAL_KEYWORDS 36
#define MIN_WORD_LENGTH 2
#define MAX_WORD_LENGTH 14
#define MIN_HASH_VALUE 5
#define MAX_HASH_VALUE 56
/* maximum key range = 52, duplicates = 0 */

#ifdef __GNUC__
__inline
#else
#ifdef __cplusplus
inline
#endif
#endif
static guint
imap_hash (register const gchar *str, register guint len)
{
  static guchar asso_values[] =
    {
      57, 57, 57, 57, 57, 57, 57, 57, 57, 57,
      57, 57, 57, 57, 57, 57, 57, 57, 57, 57,
      57, 57, 57, 57, 57, 57, 57, 57, 57, 57,
      57, 57, 57, 57, 57, 57, 57, 57, 57, 57,
      57, 57, 57, 57, 57, 57, 57, 57, 57, 57,
      57, 57, 57, 57, 57, 57, 57, 57, 57, 57,
      57, 57, 57, 57, 57, 25, 15, 10, 20,  0,
       5, 57,  5, 10, 57,  0, 30, 10, 25, 15,
       0, 57,  0, 25, 10, 10, 57, 57, 57,  5,
      57, 57, 57, 57, 57, 57, 57, 57, 57, 57,
      57, 57, 57, 57, 57, 57, 57, 57, 57, 57,
      57, 57, 57, 57, 57, 57, 57, 57, 57, 57,
      57, 57, 57, 57, 57, 57, 57, 57, 57, 57,
      57, 57, 57, 57, 57, 57, 57, 57, 57, 57,
      57, 57, 57, 57, 57, 57, 57, 57, 57, 57,
      57, 57, 57, 57, 57, 57, 57, 57, 57, 57,
      57, 57, 57, 57, 57, 57, 57, 57, 57, 57,
      57, 57, 57, 57, 57, 57, 57, 57, 57, 57,
      57, 57, 57, 57, 57, 57, 57, 57, 57, 57,
      57, 57, 57, 57, 57, 57, 57, 57, 57, 57,
      57, 57, 57, 57, 57, 57, 57, 57, 57, 57,
      57, 57, 57, 57, 57, 57, 57, 57, 57, 57,
      57, 57, 57, 57, 57, 57, 57, 57, 57, 57,
      57, 57, 57, 57, 57, 57, 57, 57, 57, 57,
      57, 57, 57, 57, 57, 57, 57, 57, 57, 57,
      57, 57, 57, 57, 57, 57
    };
  return len + asso_values[(guchar)str[len - 1]] + asso_values[(guchar)str[0]];
}

#ifdef __GNUC__
__inline
#endif
struct _imap_keyword *
imap_tokenise_struct (register const gchar *str, register guint len)
{
  static struct _imap_keyword wordlist[] =
    {
      {""}, {""}, {""}, {""}, {""},
#line 26 "camel-imapx-tokens.txt"
      {"PARSE",		IMAP_PARSE},
      {""},
#line 15 "camel-imapx-tokens.txt"
      {"EXPUNGE",	IMAP_EXPUNGE},
#line 13 "camel-imapx-tokens.txt"
      {"ENVELOPE",	IMAP_ENVELOPE},
      {""},
#line 30 "camel-imapx-tokens.txt"
      {"READ-WRITE",	IMAP_READ_WRITE},
#line 33 "camel-imapx-tokens.txt"
      {"RFC822.SIZE",	IMAP_RFC822_SIZE},
#line 28 "camel-imapx-tokens.txt"
      {"PREAUTH",	IMAP_PREAUTH},
#line 32 "camel-imapx-tokens.txt"
      {"RFC822.HEADER",	IMAP_RFC822_HEADER},
#line 29 "camel-imapx-tokens.txt"
      {"READ-ONLY",	IMAP_READ_ONLY},
#line 16 "camel-imapx-tokens.txt"
      {"FETCH",		IMAP_FETCH},
#line 31 "camel-imapx-tokens.txt"
      {"RECENT",		IMAP_RECENT},
#line 25 "camel-imapx-tokens.txt"
      {"OK",		IMAP_OK},
#line 10 "camel-imapx-tokens.txt"
      {"BYE",		IMAP_BYE},
#line 36 "camel-imapx-tokens.txt"
      {"TRYCREATE",	IMAP_TRYCREATE},
      {""},
#line 34 "camel-imapx-tokens.txt"
      {"RFC822.TEXT",	IMAP_RFC822_TEXT},
#line 18 "camel-imapx-tokens.txt"
      {"INTERNALDATE",	IMAP_INTERNALDATE},
      {""},
#line 8 "camel-imapx-tokens.txt"
      {"BODY",		IMAP_BODY},
#line 11 "camel-imapx-tokens.txt"
      {"CAPABILITY",	IMAP_CAPABILITY},
#line 38 "camel-imapx-tokens.txt"
      {"UIDVALIDITY",	IMAP_UIDVALIDITY},
#line 40 "camel-imapx-tokens.txt"
      {"UIDNEXT",	IMAP_UIDNEXT},
#line 9 "camel-imapx-tokens.txt"
      {"BODYSTRUCTURE",	IMAP_BODYSTRUCTURE},
      {""}, {""},
#line 14 "camel-imapx-tokens.txt"
      {"EXISTS",		IMAP_EXISTS},
#line 23 "camel-imapx-tokens.txt"
      {"NEWNAME",	IMAP_NEWNAME},
#line 37 "camel-imapx-tokens.txt"
      {"UID",		IMAP_UID},
#line 22 "camel-imapx-tokens.txt"
      {"NAMESPACE",	IMAP_NAMESPACE},
#line 17 "camel-imapx-tokens.txt"
      {"FLAGS",		IMAP_FLAGS},
      {""},
#line 12 "camel-imapx-tokens.txt"
      {"COPYUID",	IMAP_COPYUID},
#line 7 "camel-imapx-tokens.txt"
      {"BAD",		IMAP_BAD},
#line 27 "camel-imapx-tokens.txt"
      {"PERMANENTFLAGS",	IMAP_PERMANENTFLAGS},
#line 5 "camel-imapx-tokens.txt"
      {"ALERT",          IMAP_ALERT},
#line 39 "camel-imapx-tokens.txt"
      {"UNSEEN",		IMAP_UNSEEN},
#line 24 "camel-imapx-tokens.txt"
      {"NO",		IMAP_NO},
#line 21 "camel-imapx-tokens.txt"
      {"MESSAGES",	IMAP_MESSAGES},
#line 19 "camel-imapx-tokens.txt"
      {"LIST",		IMAP_LIST},
      {""}, {""}, {""}, {""},
#line 20 "camel-imapx-tokens.txt"
      {"LSUB",		IMAP_LSUB},
      {""}, {""}, {""}, {""},
#line 6 "camel-imapx-tokens.txt"
      {"APPENDUID",	IMAP_APPENDUID},
      {""},
#line 35 "camel-imapx-tokens.txt"
      {"STATUS",		IMAP_STATUS}
    };

  if (len <= MAX_WORD_LENGTH && len >= MIN_WORD_LENGTH)
    {
      register gint key = imap_hash (str, len);

      if (key <= MAX_HASH_VALUE && key >= 0)
        {
          register const gchar *s = wordlist[key].name;

          if (*str == *s && !strcmp (str + 1, s + 1))
            return &wordlist[key];
        }
    }
  return 0;
}
