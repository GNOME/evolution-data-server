Title: Overview

Camel is a mail access library that provides a complete, portable, and
object-oriented framework for reading, writing, sending, and storing mail.

The library covers two main areas:

**Core mail handling** (libcamel):

 - #CamelAddress, #CamelInternetAddress, #CamelNNTPAddress — address parsing
   and encoding.
 - #CamelDataWrapper, #CamelMimePart, #CamelMimeMessage, #CamelMultipart —
   the structured MIME message interface for reading and creating messages.
 - #CamelMimeFilter — stream processing modules for encoding, character set
   conversion, and more.
 - #CamelMimeParser — the core MIME parsing engine.
 - #CamelStream and subclasses — abstract I/O streams.
 - #CamelOperation — progress reporting and cancellation.
 - #CamelURL — URI parsing and encoding.

**Provider interfaces** (backend plugins):

 - #CamelSession — the main application context; must be subclassed.
 - #CamelService, #CamelStore, #CamelTransport — base classes for backends.
 - #CamelProvider — the plugin descriptor loaded from backend shared libs.
 - #CamelFolder, #CamelFolderSummary, #CamelFolderThread — folder and
   message management.
 - #CamelCipherContext, #CamelGpgContext, #CamelSMIMEContext — encryption
   and signing.
 - #CamelSasl — pluggable SASL authentication mechanisms.
 - #CamelFilterDriver — rule-based message filtering.
 - #CamelOfflineStore, #CamelOfflineFolder — offline operation support.
 - #CamelVeeFolder, #CamelVeeStore — virtual (search-based) folders.

Call camel_init() before using the library, and camel_shutdown() when done.
