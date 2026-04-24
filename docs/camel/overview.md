Title: Overview

Camel is a mail access library that provides a complete, portable, and
object-oriented framework for reading, writing, sending, and storing mail.

## Using libcamel

Call [func@init] before using the library, and [func@shutdown] when done.

The main entry point to the Camel library is the [class@CamelSession], it is the main
application context and must be subclassed.

## Core mail handling

 - [class@CamelAddress], [class@CamelInternetAddress], [class@CamelNNTPAddress] — address parsing
   and encoding.
 - [class@CamelDataWrapper], [class@CamelMimePart], [class@CamelMimeMessage], [class@CamelMultipart] —
   the structured MIME message interface for reading and creating messages.
 - [class@CamelMimeFilter] — stream processing modules for encoding, character set
   conversion, and more.
 - [class@CamelMimeParser] — the core MIME parsing engine.
 - [class@CamelStream] and subclasses — abstract I/O streams.
 - [class@CamelOperation] — progress reporting and cancellation.

## Writing a provider

 - [struct@CamelProvider] — the provider descriptor loaded from its shared libs.
 - [class@CamelService], [class@CamelStore], [class@CamelTransport] — base classes for providers.
 - [class@CamelFolder], [class@CamelFolderSummary], [class@CamelFolderThread] — folder and
   message management.

## Miscellaneous API

 - [class@CamelCipherContext], [class@CamelGpgContext], [class@CamelSMIMEContext] — encryption
   and signing.
 - [class@CamelSasl] — pluggable SASL authentication mechanisms.
 - [class@CamelFilterDriver] — rule-based message filtering.
 - [class@CamelOfflineStore], [class@CamelOfflineFolder] — offline operation support.
 - [class@CamelVeeFolder], [class@CamelVeeStore] — virtual (search-based) folders.

The `camel_mime_utils.h` header provides a wide collection of MIME and RFC 822
related utility functions used extensively throughout the library covering:

- **Character class detection** — a fast, table-driven set of macros for
  classifying individual characters according to RFC character classes
  (atoms, LWSP, tspecials, etc.).

- **Content-Transfer-Encoding** — highly optimised base64, quoted-printable,
  and UUencoding encoders and decoders.

- **Raw header parsing** — low-level tokenisers for MIME headers.

- **Structured header handling** — parsers and formatters for Date, Address,
  Content-Type, Content-Disposition, Content-ID, References, and Newsgroups
  headers with full RFC 2047 support.

- **Header folding** — utilities for folding and unfolding long header
  values according to RFC 2822.

Many of these functions represent years of production mail-client testing
and are highly optimised. For most purposes, the higher-level interfaces
on [class@CamelMimePart], [class@CamelMimeMessage], and [class@CamelInternetAddress]
should be preferred.
