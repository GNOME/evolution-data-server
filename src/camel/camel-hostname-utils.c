/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2021 Red Hat (www.redhat.com)
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
 */

/*
 * This code is based on WebKit's URL Helpers:
 * https://trac.webkit.org/browser/webkit/trunk/Source/WTF/wtf/URLHelpers.cpp?rev=278879
 */

#include "evolution-data-server-config.h"

#include <unicode/uchar.h>
#include <unicode/uscript.h>
#include <unicode/ustring.h>

#include "camel-string-utils.h"
#include "camel-hostname-utils.h"

/* This needs to be higher than the UScriptCode for any of the scripts on the IDN allowed list.
 * At one point we used USCRIPT_CODE_LIMIT from ICU, but there are two reasons not to use it.
 * 1) ICU considers it deprecated, so by setting U_HIDE_DEPRECATED we can't see it.
 * 2) No good reason to limit ourselves to scripts that existed in the ICU headers when
 *    WebKit was compiled.
 * This is only really important for platforms that load an external IDN allowed script list.
 * Not important for the compiled-in one.
 */
#define SCRIPT_CODE_LIMIT 256

static guint32 allowed_idn_script_bits[(SCRIPT_CODE_LIMIT + 31) / 32];

static gpointer
camel_hostname_utils_init_global_memory (gpointer user_data)
{
	const UScriptCode scripts[] = {
		USCRIPT_COMMON,
		USCRIPT_INHERITED,
		USCRIPT_ARABIC,
		USCRIPT_ARMENIAN,
		USCRIPT_BOPOMOFO,
		USCRIPT_CANADIAN_ABORIGINAL,
		USCRIPT_DEVANAGARI,
		USCRIPT_DESERET,
		USCRIPT_GUJARATI,
		USCRIPT_GURMUKHI,
		USCRIPT_HANGUL,
		USCRIPT_HAN,
		USCRIPT_HEBREW,
		USCRIPT_HIRAGANA,
		USCRIPT_KATAKANA_OR_HIRAGANA,
		USCRIPT_KATAKANA,
		USCRIPT_LATIN,
		USCRIPT_TAMIL,
		USCRIPT_THAI,
		USCRIPT_YI
	};
	guint ii;

	for (ii = 0; ii < G_N_ELEMENTS (scripts); ii++) {
		gint32 script = (gint32) scripts[ii];
		if (script >= 0 && script < SCRIPT_CODE_LIMIT) {
			guint32 index = script / 32;
			guint32 mask = 1 << (script % 32);
			allowed_idn_script_bits[index] |= mask;
		}
	}

	return NULL;
}

static gboolean
is_lookalike_character_for_script (UScriptCode expected_script,
				   UChar32 code_point)
{
	switch (code_point) {
	case 0x0548: /* ARMENIAN CAPITAL LETTER VO */
	case 0x054D: /* ARMENIAN CAPITAL LETTER SEH */
	case 0x0551: /* ARMENIAN CAPITAL LETTER CO */
	case 0x0555: /* ARMENIAN CAPITAL LETTER OH */
	case 0x0578: /* ARMENIAN SMALL LETTER VO */
	case 0x057D: /* ARMENIAN SMALL LETTER SEH */
	case 0x0581: /* ARMENIAN SMALL LETTER CO */
	case 0x0585: /* ARMENIAN SMALL LETTER OH */
		return expected_script == USCRIPT_ARMENIAN;
	case 0x0BE6: /* TAMIL DIGIT ZERO */
		return expected_script == USCRIPT_TAMIL;
	default:
		return FALSE;
	}
}

static gboolean
is_of_script_type (UScriptCode expected_script,
		   UChar32 code_point)
{
	UErrorCode error = U_ZERO_ERROR;
	UScriptCode script = uscript_getScript (code_point, &error);
	if (error != U_ZERO_ERROR)
		return FALSE;

	return script == expected_script;
}

static gboolean
is_ascii_digit_or_punctuation (UChar32 character)
{
	return (character >= '!' && character <= '@') || (character >= '[' && character <= '`') || (character >= '{' && character <= '~');
}

static gboolean
is_ascii_digit_or_valid_host_character (UChar32 character)
{
	if (!is_ascii_digit_or_punctuation (character))
		return FALSE;

	/* Things the URL Parser rejects: */
	switch (character) {
	case '#':
	case '%':
	case '/':
	case ':':
	case '?':
	case '@':
	case '[':
	case '\\':
	case ']':
		return FALSE;
	default:
		return TRUE;
	}
}

static gboolean
is_lookalike_sequence (UScriptCode expected_script,
		       UChar32 previous_code_point,
		       UChar32 code_point)
{
	if (!previous_code_point || previous_code_point == '/')
		return FALSE;

	return (is_lookalike_character_for_script (expected_script, code_point) && !(is_of_script_type (expected_script, previous_code_point) ||
		is_ascii_digit_or_valid_host_character (previous_code_point))) ||
	       (is_lookalike_character_for_script (expected_script, previous_code_point) && !(is_of_script_type (expected_script, code_point) ||
		is_ascii_digit_or_valid_host_character (code_point)));
}

static gboolean
is_lookalike_character (UChar32 previous_code_point,
			UChar32 code_point)
{
	/* This function treats the following as unsafe, lookalike characters:
	 * any non-printable character, any character considered as whitespace,
	 * any ignorable character, and emoji characters related to locks.
	 *
	 *  We also considered the characters in Mozilla's list of characters <http://kb.mozillazine.org/Network.IDN.blacklist_chars>.
	 *
	 * Some of the characters here will never appear once ICU has encoded.
	 * For example, ICU transforms most spaces into an ASCII space and most
	 * slashes into an ASCII solidus. But one of the two callers uses this
	 * on characters that have not been processed by ICU, so they are needed here.
	 */

	if (!u_isprint (code_point) || u_isUWhiteSpace (code_point) || u_hasBinaryProperty (code_point, UCHAR_DEFAULT_IGNORABLE_CODE_POINT))
		return TRUE;

	switch (code_point) {
	case 0x00BC: /* VULGAR FRACTION ONE QUARTER */
	case 0x00BD: /* VULGAR FRACTION ONE HALF */
	case 0x00BE: /* VULGAR FRACTION THREE QUARTERS */
	case 0x00ED: /* LATIN SMALL LETTER I WITH ACUTE */
	/* 0x0131 LATIN SMALL LETTER DOTLESS I is intentionally not considered a lookalike character because it is visually distinguishable from i and it has legitimate use in the Turkish language. */
	case 0x01C0: /* LATIN LETTER DENTAL CLICK */
	case 0x01C3: /* LATIN LETTER RETROFLEX CLICK */
	case 0x0237: /* LATIN SMALL LETTER DOTLESS J */
	case 0x0251: /* LATIN SMALL LETTER ALPHA */
	case 0x0261: /* LATIN SMALL LETTER SCRIPT G */
	case 0x0274: /* LATIN LETTER SMALL CAPITAL N */
	case 0x027E: /* LATIN SMALL LETTER R WITH FISHHOOK */
	case 0x02D0: /* MODIFIER LETTER TRIANGULAR COLON */
	case 0x0335: /* COMBINING SHORT STROKE OVERLAY */
	case 0x0337: /* COMBINING SHORT SOLIDUS OVERLAY */
	case 0x0338: /* COMBINING LONG SOLIDUS OVERLAY */
	case 0x0589: /* ARMENIAN FULL STOP */
	case 0x05B4: /* HEBREW POINT HIRIQ */
	case 0x05BC: /* HEBREW POINT DAGESH OR MAPIQ */
	case 0x05C3: /* HEBREW PUNCTUATION SOF PASUQ */
	case 0x05F4: /* HEBREW PUNCTUATION GERSHAYIM */
	case 0x0609: /* ARABIC-INDIC PER MILLE SIGN */
	case 0x060A: /* ARABIC-INDIC PER TEN THOUSAND SIGN */
	case 0x0650: /* ARABIC KASRA */
	case 0x0660: /* ARABIC INDIC DIGIT ZERO */
	case 0x066A: /* ARABIC PERCENT SIGN */
	case 0x06D4: /* ARABIC FULL STOP */
	case 0x06F0: /* EXTENDED ARABIC INDIC DIGIT ZERO */
	case 0x0701: /* SYRIAC SUPRALINEAR FULL STOP */
	case 0x0702: /* SYRIAC SUBLINEAR FULL STOP */
	case 0x0703: /* SYRIAC SUPRALINEAR COLON */
	case 0x0704: /* SYRIAC SUBLINEAR COLON */
	case 0x1735: /* PHILIPPINE SINGLE PUNCTUATION */
	case 0x1D04: /* LATIN LETTER SMALL CAPITAL C */
	case 0x1D0F: /* LATIN LETTER SMALL CAPITAL O */
	case 0x1D1C: /* LATIN LETTER SMALL CAPITAL U */
	case 0x1D20: /* LATIN LETTER SMALL CAPITAL V */
	case 0x1D21: /* LATIN LETTER SMALL CAPITAL W */
	case 0x1D22: /* LATIN LETTER SMALL CAPITAL Z */
	case 0x1ECD: /* LATIN SMALL LETTER O WITH DOT BELOW */
	case 0x2010: /* HYPHEN */
	case 0x2011: /* NON-BREAKING HYPHEN */
	case 0x2024: /* ONE DOT LEADER */
	case 0x2027: /* HYPHENATION POINT */
	case 0x2039: /* SINGLE LEFT-POINTING ANGLE QUOTATION MARK */
	case 0x203A: /* SINGLE RIGHT-POINTING ANGLE QUOTATION MARK */
	case 0x2041: /* CARET INSERTION POINT */
	case 0x2044: /* FRACTION SLASH */
	case 0x2052: /* COMMERCIAL MINUS SIGN */
	case 0x2153: /* VULGAR FRACTION ONE THIRD */
	case 0x2154: /* VULGAR FRACTION TWO THIRDS */
	case 0x2155: /* VULGAR FRACTION ONE FIFTH */
	case 0x2156: /* VULGAR FRACTION TWO FIFTHS */
	case 0x2157: /* VULGAR FRACTION THREE FIFTHS */
	case 0x2158: /* VULGAR FRACTION FOUR FIFTHS */
	case 0x2159: /* VULGAR FRACTION ONE SIXTH */
	case 0x215A: /* VULGAR FRACTION FIVE SIXTHS */
	case 0x215B: /* VULGAR FRACTION ONE EIGHT */
	case 0x215C: /* VULGAR FRACTION THREE EIGHTHS */
	case 0x215D: /* VULGAR FRACTION FIVE EIGHTHS */
	case 0x215E: /* VULGAR FRACTION SEVEN EIGHTHS */
	case 0x215F: /* FRACTION NUMERATOR ONE */
	case 0x2212: /* MINUS SIGN */
	case 0x2215: /* DIVISION SLASH */
	case 0x2216: /* SET MINUS */
	case 0x2236: /* RATIO */
	case 0x233F: /* APL FUNCTIONAL SYMBOL SLASH BAR */
	case 0x23AE: /* INTEGRAL EXTENSION */
	case 0x244A: /* OCR DOUBLE BACKSLASH */
	case 0x2571: /* BOX DRAWINGS LIGHT DIAGONAL UPPER RIGHT TO LOWER LEFT */
	case 0x2572: /* BOX DRAWINGS LIGHT DIAGONAL UPPER LEFT TO LOWER RIGHT */
	case 0x29F6: /* SOLIDUS WITH OVERBAR */
	case 0x29F8: /* BIG SOLIDUS */
	case 0x2AFB: /* TRIPLE SOLIDUS BINARY RELATION */
	case 0x2AFD: /* DOUBLE SOLIDUS OPERATOR */
	case 0x2FF0: /* IDEOGRAPHIC DESCRIPTION CHARACTER LEFT TO RIGHT */
	case 0x2FF1: /* IDEOGRAPHIC DESCRIPTION CHARACTER ABOVE TO BELOW */
	case 0x2FF2: /* IDEOGRAPHIC DESCRIPTION CHARACTER LEFT TO MIDDLE AND RIGHT */
	case 0x2FF3: /* IDEOGRAPHIC DESCRIPTION CHARACTER ABOVE TO MIDDLE AND BELOW */
	case 0x2FF4: /* IDEOGRAPHIC DESCRIPTION CHARACTER FULL SURROUND */
	case 0x2FF5: /* IDEOGRAPHIC DESCRIPTION CHARACTER SURROUND FROM ABOVE */
	case 0x2FF6: /* IDEOGRAPHIC DESCRIPTION CHARACTER SURROUND FROM BELOW */
	case 0x2FF7: /* IDEOGRAPHIC DESCRIPTION CHARACTER SURROUND FROM LEFT */
	case 0x2FF8: /* IDEOGRAPHIC DESCRIPTION CHARACTER SURROUND FROM UPPER LEFT */
	case 0x2FF9: /* IDEOGRAPHIC DESCRIPTION CHARACTER SURROUND FROM UPPER RIGHT */
	case 0x2FFA: /* IDEOGRAPHIC DESCRIPTION CHARACTER SURROUND FROM LOWER LEFT */
	case 0x2FFB: /* IDEOGRAPHIC DESCRIPTION CHARACTER OVERLAID */
	case 0x3002: /* IDEOGRAPHIC FULL STOP */
	case 0x3008: /* LEFT ANGLE BRACKET */
	case 0x3014: /* LEFT TORTOISE SHELL BRACKET */
	case 0x3015: /* RIGHT TORTOISE SHELL BRACKET */
	case 0x3033: /* VERTICAL KANA REPEAT MARK UPPER HALF */
	case 0x3035: /* VERTICAL KANA REPEAT MARK LOWER HALF */
	case 0x321D: /* PARENTHESIZED KOREAN CHARACTER OJEON */
	case 0x321E: /* PARENTHESIZED KOREAN CHARACTER O HU */
	case 0x33AE: /* SQUARE RAD OVER S */
	case 0x33AF: /* SQUARE RAD OVER S SQUARED */
	case 0x33C6: /* SQUARE C OVER KG */
	case 0x33DF: /* SQUARE A OVER M */
	case 0x05B9: /* HEBREW POINT HOLAM */
	case 0x05BA: /* HEBREW POINT HOLAM HASER FOR VAV */
	case 0x05C1: /* HEBREW POINT SHIN DOT */
	case 0x05C2: /* HEBREW POINT SIN DOT */
	case 0x05C4: /* HEBREW MARK UPPER DOT */
	case 0xA731: /* LATIN LETTER SMALL CAPITAL S */
	case 0xA771: /* LATIN SMALL LETTER DUM */
	case 0xA789: /* MODIFIER LETTER COLON */
	case 0xFE14: /* PRESENTATION FORM FOR VERTICAL SEMICOLON */
	case 0xFE15: /* PRESENTATION FORM FOR VERTICAL EXCLAMATION MARK */
	case 0xFE3F: /* PRESENTATION FORM FOR VERTICAL LEFT ANGLE BRACKET */
	case 0xFE5D: /* SMALL LEFT TORTOISE SHELL BRACKET */
	case 0xFE5E: /* SMALL RIGHT TORTOISE SHELL BRACKET */
	case 0xFF0E: /* FULLWIDTH FULL STOP */
	case 0xFF0F: /* FULL WIDTH SOLIDUS */
	case 0xFF61: /* HALFWIDTH IDEOGRAPHIC FULL STOP */
	case 0xFFFC: /* OBJECT REPLACEMENT CHARACTER */
	case 0xFFFD: /* REPLACEMENT CHARACTER */
	case 0x1F50F: /* LOCK WITH INK PEN */
	case 0x1F510: /* CLOSED LOCK WITH KEY */
	case 0x1F511: /* KEY */
	case 0x1F512: /* LOCK */
	case 0x1F513: /* OPEN LOCK */
		return TRUE;
	case 0x0307: /* COMBINING DOT ABOVE */
		return previous_code_point == 0x0237 || /* LATIN SMALL LETTER DOTLESS J */
			previous_code_point == 0x0131 || /* LATIN SMALL LETTER DOTLESS I */
			previous_code_point == 0x05D5; /* HEBREW LETTER VAV */
	case '.':
		return FALSE;
	default:
		return is_lookalike_sequence (USCRIPT_ARMENIAN, previous_code_point, code_point) ||
			is_lookalike_sequence (USCRIPT_TAMIL, previous_code_point, code_point);
	}
}

static gboolean
all_characters_in_allowed_idn_script_list (const UChar *buffer,
					   gint32 length)
{
	gint32 ii = 0;
	UChar32 previous_code_point = 0;

	while (ii < length) {
		UChar32 cc;
		UErrorCode error;
		UScriptCode script;
		guint32 index, mask;

		U16_NEXT (buffer, ii, length, cc);
		error = U_ZERO_ERROR;
		script = uscript_getScript (cc, &error);
		if (error != U_ZERO_ERROR) {
			return FALSE;
		}
		if (script < 0) {
			return FALSE;
		}
		if (script >= SCRIPT_CODE_LIMIT)
			return FALSE;

		index = script / 32;
		mask = 1 << (script % 32);

		if (!(allowed_idn_script_bits[index] & mask))
			return FALSE;

		if (is_lookalike_character (previous_code_point, cc))
			return FALSE;

		previous_code_point = cc;
	}

	return TRUE;
}

static gboolean
is_second_level_domain_name_allowed_by_tld_rules (const UChar *buffer,
						  gint32 length,
						  gboolean (* character_is_allowed) (UChar ch))
{
	gint32 ii;

	g_return_val_if_fail (length > 0, FALSE);

	for (ii = length - 1; ii >= 0; ii--) {
		UChar ch = buffer[ii];

		if (character_is_allowed (ch))
			continue;

		/* Only check the second level domain. Lower level registrars may have different rules. */
		if (ch == '.')
			break;

		return FALSE;
	}

	return TRUE;
}

static gboolean
check_rules_if_suffix_matches (const UChar *buffer,
			       gint length,
			       const UChar suffix[],
			       guint n_suffix,
			       guint sizeof_suffix,
			       gboolean (* func) (const UChar ch),
			       gboolean *out_result)
{
        if (length > n_suffix && !memcmp (buffer + length - n_suffix, suffix, sizeof_suffix)) {
		*out_result = is_second_level_domain_name_allowed_by_tld_rules (buffer, length - n_suffix, func);
		return TRUE;
	}

	return FALSE;
}

static gboolean
is_russian_domain_name_character (const UChar ch)
{
	/* Only modern Russian letters, digits and dashes are allowed. */
	return (ch >= 0x0430 && ch <= 0x044f) || ch == 0x0451 || g_ascii_isdigit (ch) || ch == '-';
}

static gboolean
is_russian_and_byelorussian_domain_name_character (const UChar ch)
{
	/* Russian and Byelorussian letters, digits and dashes are allowed. */
	return (ch >= 0x0430 && ch <= 0x044f) || ch == 0x0451 || ch == 0x0456 || ch == 0x045E || ch == 0x2019 || g_ascii_isdigit (ch) || ch == '-';
}

static gboolean
is_kazakh_domain_name_character (const UChar ch)
{
	/* Kazakh letters, digits and dashes are allowed. */
	return (ch >= 0x0430 && ch <= 0x044f) || ch == 0x0451 || ch == 0x04D9 || ch == 0x0493 || ch == 0x049B || ch == 0x04A3 ||
		ch == 0x04E9 || ch == 0x04B1 || ch == 0x04AF || ch == 0x04BB || ch == 0x0456 || g_ascii_isdigit (ch) || ch == '-';
}

static gboolean
is_russian_and_ukrainian_domain_name_character (const UChar ch)
{
	/* Russian and Ukrainian letters, digits and dashes are allowed. */
	return (ch >= 0x0430 && ch <= 0x044f) || ch == 0x0451 || ch == 0x0491 || ch == 0x0404 || ch == 0x0456 || ch == 0x0457 || g_ascii_isdigit (ch) || ch == '-';
}

static gboolean
is_serbian_domain_name_character (const UChar ch)
{
	/* Serbian letters, digits and dashes are allowed. */
	return (ch >= 0x0430 && ch <= 0x0438) || (ch >= 0x043A && ch <= 0x0448) || ch == 0x0452 || ch == 0x0458 || ch == 0x0459 ||
		ch == 0x045A || ch == 0x045B || ch == 0x045F || g_ascii_isdigit (ch) || ch == '-';
}

static gboolean
is_macedonian_domain_name_character (const UChar ch)
{
	/* Macedonian letters, digits and dashes are allowed. */
	return (ch >= 0x0430 && ch <= 0x0438) || (ch >= 0x043A && ch <= 0x0448) || ch == 0x0453 || ch == 0x0455 || ch == 0x0458 ||
		ch == 0x0459 || ch == 0x045A || ch == 0x045C || ch == 0x045F || g_ascii_isdigit (ch) || ch == '-';
}

static gboolean
is_mongolian_domain_name_character (const UChar ch)
{
	/* Mongolian letters, digits and dashes are allowed. */
	return (ch >= 0x0430 && ch <= 0x044f) || ch == 0x0451 || ch == 0x04E9 || ch == 0x04AF || g_ascii_isdigit (ch) || ch == '-';
}

static gboolean
is_bulgarian_domain_name_character (const UChar ch)
{
	/* Bulgarian letters, digits and dashes are allowed. */
	return (ch >= 0x0430 && ch <= 0x044A) || ch == 0x044C || (ch >= 0x044E && ch <= 0x0450) || ch == 0x045D || g_ascii_isdigit (ch) || ch == '-';
}

static gboolean
all_characters_allowed_by_tld_rules (const UChar *buffer,
				     gint32 length)
{
	/* Skip trailing dot for root domain. */
	if (buffer[length - 1] == '.')
		length--;

	#define CHECK_RULES_IF_SUFFIX_MATCHES(suffix, func)  G_STMT_START { \
		gboolean result = FALSE; \
		if (check_rules_if_suffix_matches (buffer, length, suffix, G_N_ELEMENTS (suffix), sizeof (suffix), func, &result)) \
			return result; \
		} G_STMT_END

	{
	/* http://cctld.ru/files/pdf/docs/rules_ru-rf.pdf */
	static const UChar cyrillic_RF[] = {
		'.',
		0x0440, /* CYRILLIC SMALL LETTER ER */
		0x0444, /* CYRILLIC SMALL LETTER EF */
		};
	CHECK_RULES_IF_SUFFIX_MATCHES (cyrillic_RF, is_russian_domain_name_character);
	}

	{
	/* http://rusnames.ru/rules.pl */
	static const UChar cyrillic_RUS[] = {
		'.',
		0x0440, /* CYRILLIC SMALL LETTER ER */
		0x0443, /* CYRILLIC SMALL LETTER U */
		0x0441, /* CYRILLIC SMALL LETTER ES */
		};
	CHECK_RULES_IF_SUFFIX_MATCHES (cyrillic_RUS, is_russian_domain_name_character);
	}

	{
	/* http://ru.faitid.org/projects/moscow/documents/moskva/idn */
	static const UChar cyrillic_MOSKVA[] = {
		'.',
		0x043C, /* CYRILLIC SMALL LETTER EM */
		0x043E, /* CYRILLIC SMALL LETTER O */
		0x0441, /* CYRILLIC SMALL LETTER ES */
		0x043A, /* CYRILLIC SMALL LETTER KA */
		0x0432, /* CYRILLIC SMALL LETTER VE */
		0x0430, /* CYRILLIC SMALL LETTER A */
		};
	CHECK_RULES_IF_SUFFIX_MATCHES (cyrillic_MOSKVA, is_russian_domain_name_character);
	}

	{
	/* http://www.dotdeti.ru/foruser/docs/regrules.php */
	static const UChar cyrillic_DETI[] = {
		'.',
		0x0434, /* CYRILLIC SMALL LETTER DE */
		0x0435, /* CYRILLIC SMALL LETTER IE */
		0x0442, /* CYRILLIC SMALL LETTER TE */
		0x0438, /* CYRILLIC SMALL LETTER I */
		};
	CHECK_RULES_IF_SUFFIX_MATCHES (cyrillic_DETI, is_russian_domain_name_character);
	}

	{
	/* http://corenic.org - rules not published. The word is Russian, so only allowing Russian at this time,
	   although we may need to revise the checks if this ends up being used with other languages spoken in Russia. */
	static const UChar cyrillic_ONLAYN[] = {
		'.',
		0x043E, /* CYRILLIC SMALL LETTER O */
		0x043D, /* CYRILLIC SMALL LETTER EN */
		0x043B, /* CYRILLIC SMALL LETTER EL */
		0x0430, /* CYRILLIC SMALL LETTER A */
		0x0439, /* CYRILLIC SMALL LETTER SHORT I */
		0x043D, /* CYRILLIC SMALL LETTER EN */
		};
	CHECK_RULES_IF_SUFFIX_MATCHES (cyrillic_ONLAYN, is_russian_domain_name_character);
	}

	{
	/* http://corenic.org - same as above. */
	static const UChar cyrillic_SAYT[] = {
		'.',
		0x0441, /* CYRILLIC SMALL LETTER ES */
		0x0430, /* CYRILLIC SMALL LETTER A */
		0x0439, /* CYRILLIC SMALL LETTER SHORT I */
		0x0442, /* CYRILLIC SMALL LETTER TE */
		};
	CHECK_RULES_IF_SUFFIX_MATCHES (cyrillic_SAYT, is_russian_domain_name_character);
	}

	{
	/* http://pir.org/products/opr-domain/ - rules not published. According to the registry site,
	   the intended audience is "Russian and other Slavic-speaking markets".
	   Chrome appears to only allow Russian, so sticking with that for now. */
	static const UChar cyrillic_ORG[] = {
		'.',
		0x043E, /* CYRILLIC SMALL LETTER O */
		0x0440, /* CYRILLIC SMALL LETTER ER */
		0x0433, /* CYRILLIC SMALL LETTER GHE */
		};
	CHECK_RULES_IF_SUFFIX_MATCHES (cyrillic_ORG, is_russian_domain_name_character);
	}

	{
	/* http://cctld.by/rules.html */
	static const UChar cyrillic_BEL[] = {
		'.',
		0x0431, /* CYRILLIC SMALL LETTER BE */
		0x0435, /* CYRILLIC SMALL LETTER IE */
		0x043B, /* CYRILLIC SMALL LETTER EL */
		};
	CHECK_RULES_IF_SUFFIX_MATCHES (cyrillic_BEL, is_russian_and_byelorussian_domain_name_character);
	}

	{
	/* http://www.nic.kz/docs/poryadok_vnedreniya_kaz_ru.pdf */
	static const UChar cyrillic_KAZ[] = {
		'.',
		0x049B, /* CYRILLIC SMALL LETTER KA WITH DESCENDER */
		0x0430, /* CYRILLIC SMALL LETTER A */
		0x0437, /* CYRILLIC SMALL LETTER ZE */
		};
	CHECK_RULES_IF_SUFFIX_MATCHES (cyrillic_KAZ, is_kazakh_domain_name_character);
	}

	{
	/* http://uanic.net/docs/documents-ukr/Rules%20of%20UKR_v4.0.pdf */
	static const UChar cyrillic_UKR[] = {
		'.',
		0x0443, /* CYRILLIC SMALL LETTER U */
		0x043A, /* CYRILLIC SMALL LETTER KA */
		0x0440, /* CYRILLIC SMALL LETTER ER */
		};
	CHECK_RULES_IF_SUFFIX_MATCHES (cyrillic_UKR, is_russian_and_ukrainian_domain_name_character);
	}

	{
	/* http://www.rnids.rs/data/DOKUMENTI/idn-srb-policy-termsofuse-v1.4-eng.pdf */
	static const UChar cyrillic_SRB[] = {
		'.',
		0x0441, /* CYRILLIC SMALL LETTER ES */
		0x0440, /* CYRILLIC SMALL LETTER ER */
		0x0431, /* CYRILLIC SMALL LETTER BE */
		};
	CHECK_RULES_IF_SUFFIX_MATCHES (cyrillic_SRB, is_serbian_domain_name_character);
	}

	{
	/* http://marnet.mk/doc/pravilnik-mk-mkd.pdf */
	static const UChar cyrillic_MKD[] = {
		'.',
		0x043C, /* CYRILLIC SMALL LETTER EM */
		0x043A, /* CYRILLIC SMALL LETTER KA */
		0x0434, /* CYRILLIC SMALL LETTER DE */
		};
	CHECK_RULES_IF_SUFFIX_MATCHES (cyrillic_MKD, is_macedonian_domain_name_character);
	}

	{
	/* https://www.mon.mn/cs/ */
	static const UChar cyrillic_MON[] = {
		'.',
		0x043C, /* CYRILLIC SMALL LETTER EM */
		0x043E, /* CYRILLIC SMALL LETTER O */
		0x043D, /* CYRILLIC SMALL LETTER EN */
		};
	CHECK_RULES_IF_SUFFIX_MATCHES (cyrillic_MON, is_mongolian_domain_name_character);
	}

	{
	/* https://www.icann.org/sites/default/files/packages/lgr/lgr-second-level-bulgarian-30aug16-en.html */
	static const UChar cyrillic_BG[] = {
		'.',
		0x0431, /* CYRILLIC SMALL LETTER BE */
		0x0433 /* CYRILLIC SMALL LETTER GHE */
		};
	CHECK_RULES_IF_SUFFIX_MATCHES (cyrillic_BG, is_bulgarian_domain_name_character);
	}

    /* Not a known top level domain with special rules. */
    return FALSE;
}

/**
 * camel_hostname_utils_requires_ascii:
 * @hostname: a host name
 *
 * Check whether the @hostname requires conversion to ASCII. That can
 * be when a character in it can look like an ASCII character, even
 * it being a Unicode letter. This can be used to display host names
 * in a way of invulnerable to IDN homograph attacks.
 *
 * Returns: %TRUE, when the @hostname should be converted to an ASCII equivalent,
 *    %FALSE, when it can be shown as is.
 *
 * Since: 3.44
 **/
gboolean
camel_hostname_utils_requires_ascii (const gchar *hostname)
{
	static GOnce initialized = G_ONCE_INIT;
	UErrorCode uerror = U_ZERO_ERROR;
	int32_t uhost_len = 0;
	gboolean needs_conversion = FALSE;

	if (camel_string_is_all_ascii (hostname))
		return FALSE;

	g_once (&initialized, camel_hostname_utils_init_global_memory, NULL);

	u_strFromUTF8 (NULL, 0, &uhost_len, hostname, -1, &uerror);
	if (uhost_len > 0) {
		UChar *uhost = g_new0 (UChar, uhost_len + 2);

		uerror = U_ZERO_ERROR;
		u_strFromUTF8 (uhost, uhost_len + 1, &uhost_len, hostname, -1, &uerror);
		if (uerror == U_ZERO_ERROR && uhost_len > 0) {
			needs_conversion = !all_characters_in_allowed_idn_script_list (uhost, uhost_len) ||
					   !all_characters_allowed_by_tld_rules (uhost, uhost_len);
		} else {
			needs_conversion = uerror != U_ZERO_ERROR;
		}

		g_free (uhost);
	} else {
		needs_conversion = TRUE;
	}

	return needs_conversion;
}
