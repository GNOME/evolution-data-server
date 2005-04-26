#!/usr/bin/perl

%special = (
    "CHARS_LWSP", " \t\n\r",
    "CHARS_TSPECIAL", "()<>@,;:\\\"/[]?=",
    "CHARS_SPECIAL", "()<>@,;:\\\".[]",
    "CHARS_CSPECIAL", "()\\\r",	# not in comments
    "CHARS_DSPECIAL", "[]\\\r \t",	# not in domains
    "CHARS_ATTRCHAR", "*\'% " );	# extra non-included attribute-chars

%bits = (
    CAMEL_MIME_IS_CTRL	, 1<<0,
    CAMEL_MIME_IS_LWSP	, 1<<1,
    CAMEL_MIME_IS_TSPECIAL	, 1<<2,
    CAMEL_MIME_IS_SPECIAL	, 1<<3,
    CAMEL_MIME_IS_SPACE	, 1<<4,
    CAMEL_MIME_IS_DSPECIAL	, 1<<5,
    CAMEL_MIME_IS_QPSAFE	, 1<<6,
    CAMEL_MIME_IS_ESAFE	, 1<<7,	#/* encoded word safe */
    CAMEL_MIME_IS_PSAFE	, 1<<8,	#/* encoded word in phrase safe */
    CAMEL_MIME_IS_ATTRCHAR  , 1<<9,	#/* attribute-char safe (rfc2184) */
);

@table = ();

# set bit in character positions
sub add_bits {
    my $bit = $_[0];
    my $str = $_[1];
    my $ch;

    foreach $ch (split(/.{0}/, $str)) {
	$table[ord($ch)] |= $bits{$bit};
    }
};

# remove bit in character positions
sub rem_bits {
    my $bit = $_[0];
    my $str = $_[1];
    my $ch;

    foreach $ch (split(/.{0}/, $str)) {
	$table[ord($ch)] &= ~($bits{$bit});
    }
};

# set up base ranges
foreach $i (0 .. 255) {
    $table[$i] = 0;
    if ($i<32 || $i==127) {
	$table[$i] |= $bits{CAMEL_MIME_IS_CTRL} | $bits{CAMEL_MIME_IS_TSPECIAL};
    } elsif ($i < 127) {
	$table[$i] |= $bits{CAMEL_MIME_IS_ATTRCHAR};
    }
    if (($i>=32 && $i<=60) || ($i>=62 && $i<=126) || $i==9) {
	$table[$i] |= ($bits{CAMEL_MIME_IS_QPSAFE} | $bits{CAMEL_MIME_IS_ESAFE});
    }
    if (($i>=0x30 && $i<=0x39) || ($i>=0x61 && $i<=0x7a) || ($i>=0x41 && $i<= 0x5a)) {
	$table[$i] |= $bits{CAMEL_MIME_IS_PSAFE};
    }
}

$table[0x20] |= $bits{CAMEL_MIME_IS_SPACE};

add_bits('CAMEL_MIME_IS_LWSP', " \t\n\r");
add_bits('CAMEL_MIME_IS_TSPECIAL', "()<>@,;:\\\"/[]?=");
add_bits('CAMEL_MIME_IS_SPECIAL', "()<>@,;:\\\".[]");
# not in domains
add_bits('CAMEL_MIME_IS_DSPECIAL', "[]\\\r \t");

# list of characters that must be encoded.
# encoded word in text specials: rfc 2047 5(1)
rem_bits('CAMEL_MIME_IS_ESAFE', "()<>@,;:\"/[]?.=_");

# non-included attribute-chars (tspecial + extra)
rem_bits('CAMEL_MIME_IS_ATTRCHAR', "*\'% "."()<>@,;:\\\"/[]?=");

# list of additional characters that can be left unencoded.
# encoded word in phrase specials: rfc 2047 5(3)
add_bits('CAMEL_MIME_IS_PSAFE', "!*+-/");


#header_init_bits(CAMEL_MIME_IS_LWSP, 0, 0, CHARS_LWSP);
#header_init_bits(CAMEL_MIME_IS_TSPECIAL, CAMEL_MIME_IS_CTRL, 0, CHARS_TSPECIAL);
#header_init_bits(CAMEL_MIME_IS_SPECIAL, 0, 0, CHARS_SPECIAL);
#header_init_bits(CAMEL_MIME_IS_DSPECIAL, 0, FALSE, CHARS_DSPECIAL);
#header_remove_bits(CAMEL_MIME_IS_ESAFE, CHARS_ESPECIAL);
#header_remove_bits(CAMEL_MIME_IS_ATTRCHAR, CHARS_TSPECIAL CHARS_ATTRCHAR);
#header_init_bits(CAMEL_MIME_IS_PSAFE, 0, 0, CHARS_PSPECIAL);

# output
print "const unsigned short camel_mime_special_table[256] = {\n\t";
foreach $i (0..255) {
    printf "0x%04x,", $table[$i];
    if (($i & 0x7) == 0x7) {
	print "\n";
	if ($i != 0xff) {
	    print "\t";
	}
    } else {
	print " ";
    }
}
print "};\n";
