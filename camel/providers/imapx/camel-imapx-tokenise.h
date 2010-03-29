/* ANSI-C code produced by gperf version 3.0.2 */
/* Command-line: /usr/bin/gperf -H imapx_hash -N imapx_tokenise_struct -L ANSI-C -o -t -k'1,$' --output-file=camel-imapx-tokenise.h camel-imapx-tokens.txt  */

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
struct _imapx_keyword {const gchar *name; camel_imapx_id_t id; };
struct _imapx_keyword *imapx_tokenise_struct (register const char *str, register unsigned int len);

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
static unsigned int
imapx_hash (register const char *str, register unsigned int len)
{
  static unsigned char asso_values[] =
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
  return len + asso_values[(unsigned char)str[len - 1]] + asso_values[(unsigned char)str[0]];
}

#ifdef __GNUC__
__inline
#endif
struct _imapx_keyword *
imapx_tokenise_struct (register const char *str, register unsigned int len)
{
  static struct _imapx_keyword wordlist[] =
    {
      {""}, {""}, {""}, {""}, {""},
#line 27 "camel-imapx-tokens.txt"
      {"PARSE",		IMAPX_PARSE},
      {""},
#line 16 "camel-imapx-tokens.txt"
      {"EXPUNGE",	IMAPX_EXPUNGE},
#line 14 "camel-imapx-tokens.txt"
      {"ENVELOPE",	IMAPX_ENVELOPE},
      {""},
#line 31 "camel-imapx-tokens.txt"
      {"READ-WRITE",	IMAPX_READ_WRITE},
#line 34 "camel-imapx-tokens.txt"
      {"RFC822.SIZE",	IMAPX_RFC822_SIZE},
#line 29 "camel-imapx-tokens.txt"
      {"PREAUTH",	IMAPX_PREAUTH},
#line 33 "camel-imapx-tokens.txt"
      {"RFC822.HEADER",	IMAPX_RFC822_HEADER},
#line 30 "camel-imapx-tokens.txt"
      {"READ-ONLY",	IMAPX_READ_ONLY},
#line 17 "camel-imapx-tokens.txt"
      {"FETCH",		IMAPX_FETCH},
#line 32 "camel-imapx-tokens.txt"
      {"RECENT",		IMAPX_RECENT},
#line 26 "camel-imapx-tokens.txt"
      {"OK",		IMAPX_OK},
#line 11 "camel-imapx-tokens.txt"
      {"BYE",		IMAPX_BYE},
#line 37 "camel-imapx-tokens.txt"
      {"TRYCREATE",	IMAPX_TRYCREATE},
      {""},
#line 35 "camel-imapx-tokens.txt"
      {"RFC822.TEXT",	IMAPX_RFC822_TEXT},
#line 19 "camel-imapx-tokens.txt"
      {"INTERNALDATE",	IMAPX_INTERNALDATE},
      {""},
#line 9 "camel-imapx-tokens.txt"
      {"BODY",		IMAPX_BODY},
#line 12 "camel-imapx-tokens.txt"
      {"CAPABILITY",	IMAPX_CAPABILITY},
#line 39 "camel-imapx-tokens.txt"
      {"UIDVALIDITY",	IMAPX_UIDVALIDITY},
#line 41 "camel-imapx-tokens.txt"
      {"UIDNEXT",	IMAPX_UIDNEXT},
#line 10 "camel-imapx-tokens.txt"
      {"BODYSTRUCTURE",	IMAPX_BODYSTRUCTURE},
      {""}, {""},
#line 15 "camel-imapx-tokens.txt"
      {"EXISTS",		IMAPX_EXISTS},
#line 24 "camel-imapx-tokens.txt"
      {"NEWNAME",	IMAPX_NEWNAME},
#line 38 "camel-imapx-tokens.txt"
      {"UID",		IMAPX_UID},
#line 23 "camel-imapx-tokens.txt"
      {"NAMESPACE",	IMAPX_NAMESPACE},
#line 18 "camel-imapx-tokens.txt"
      {"FLAGS",		IMAPX_FLAGS},
      {""},
#line 13 "camel-imapx-tokens.txt"
      {"COPYUID",	IMAPX_COPYUID},
#line 8 "camel-imapx-tokens.txt"
      {"BAD",		IMAPX_BAD},
#line 28 "camel-imapx-tokens.txt"
      {"PERMANENTFLAGS",	IMAPX_PERMANENTFLAGS},
#line 6 "camel-imapx-tokens.txt"
      {"ALERT",          IMAPX_ALERT},
#line 40 "camel-imapx-tokens.txt"
      {"UNSEEN",		IMAPX_UNSEEN},
#line 25 "camel-imapx-tokens.txt"
      {"NO",		IMAPX_NO},
#line 22 "camel-imapx-tokens.txt"
      {"MESSAGES",	IMAPX_MESSAGES},
#line 20 "camel-imapx-tokens.txt"
      {"LIST",		IMAPX_LIST},
      {""}, {""}, {""}, {""},
#line 21 "camel-imapx-tokens.txt"
      {"LSUB",		IMAPX_LSUB},
      {""}, {""}, {""}, {""},
#line 7 "camel-imapx-tokens.txt"
      {"APPENDUID",	IMAPX_APPENDUID},
      {""},
#line 36 "camel-imapx-tokens.txt"
      {"STATUS",		IMAPX_STATUS}
    };

  if (len <= MAX_WORD_LENGTH && len >= MIN_WORD_LENGTH)
    {
      register int key = imapx_hash (str, len);

      if (key <= MAX_HASH_VALUE && key >= 0)
        {
          register const char *s = wordlist[key].name;

          if (*str == *s && !strcmp (str + 1, s + 1))
            return &wordlist[key];
        }
    }
  return 0;
}
