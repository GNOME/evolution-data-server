import pytest
import re


# Simulate the vulnerable filter construction logic (Python equivalent)
def build_ldap_filter_vulnerable(search_filter: str, user_value: str) -> str:
    """Simulates the vulnerable C code that directly concatenates user input."""
    if search_filter and search_filter.lower() != "(objectclass=*)":
        return f"(& {search_filter} {user_value})"
    return user_value


def build_ldap_filter_safe(search_filter: str, user_value: str) -> str:
    """Simulates a safe implementation that escapes LDAP filter metacharacters per RFC 4515."""
    escaped = escape_ldap_filter_value(user_value)
    if search_filter and search_filter.lower() != "(objectclass=*)":
        return f"(& {search_filter} {escaped})"
    return escaped


def escape_ldap_filter_value(value: str) -> str:
    """Escape special LDAP filter characters per RFC 4515."""
    # Characters that must be escaped in LDAP filters
    escape_map = {
        '\\': '\\5c',
        '*': '\\2a',
        '(': '\\28',
        ')': '\\29',
        '\x00': '\\00',
    }
    result = []
    for char in value:
        result.append(escape_map.get(char, char))
    return ''.join(result)


def contains_unescaped_ldap_metacharacters(value: str) -> bool:
    """Check if a string contains unescaped LDAP filter metacharacters."""
    # Look for unescaped parentheses, asterisks, null bytes
    # Escaped sequences look like \XX where XX is hex
    # We check for raw metacharacters not preceded by backslash escape
    i = 0
    while i < len(value):
        char = value[i]
        if char == '\\' and i + 2 < len(value):
            # Skip escaped sequence
            i += 3
            continue
        if char in ('(', ')', '*', '\x00'):
            return True
        i += 1
    return False


def is_valid_ldap_filter_structure(filter_str: str) -> bool:
    """Basic structural validation of LDAP filter."""
    # Count unescaped parentheses
    depth = 0
    i = 0
    while i < len(filter_str):
        char = filter_str[i]
        if char == '\\' and i + 2 < len(filter_str):
            i += 3
            continue
        if char == '(':
            depth += 1
        elif char == ')':
            depth -= 1
            if depth < 0:
                return False
        i += 1
    return depth == 0


@pytest.mark.parametrize("payload", [
    # Basic LDAP injection attempts
    "*",
    "*(objectClass=*)",
    "*(|(objectClass=*))",
    "*)(objectClass=*",
    "admin)(&(password=*))",
    "*(uid=*)",
    ")(|(uid=*)",
    "*))(|(uid=*",
    # Filter logic injection
    "(|(uid=*))",
    "(&(uid=*)(password=*))",
    "(objectClass=*)",
    "*(|(password=*))",
    # Null byte injection
    "test\x00injected",
    "\x00",
    # Wildcard abuse
    "a*b*c",
    "**",
    # Nested filter injection
    ")(objectClass=*)(uid=",
    ")(cn=*)(sn=",
    # Boolean logic manipulation
    "x)(|(a=*)(b=*))(c=",
    "admin)(password=*",
    # RFC 4515 special characters
    "test\\2a",  # This is a legitimate escaped asterisk - should pass through
    "(test",
    "test)",
    "test*test",
    # Complex injection payloads
    "john)(|(uid=*)(cn=*))(sn=",
    ")(uid=admin)(|(uid=",
    "a)(b=c",
    # Empty and whitespace
    "",
    "   ",
    # Long injection
    "a" * 1000 + ")(objectClass=*",
    # Unicode that might bypass filters
    "test\uff08injection\uff09",
])
def test_ldap_filter_injection_prevention(payload):
    """Invariant: User-supplied values must be properly escaped before inclusion in LDAP
    filter strings. The resulting filter must not allow injection of LDAP metacharacters
    that could alter filter logic. Unescaped parentheses, asterisks, and null bytes from
    user input must never appear in the final filter string in a way that modifies
    filter structure."""

    base_filter = "(objectClass=person)"

    # Build filter using safe implementation
    safe_filter = build_ldap_filter_safe(base_filter, payload)

    # Build filter using vulnerable implementation for comparison
    vulnerable_filter = build_ldap_filter_vulnerable(base_filter, payload)

    # INVARIANT 1: The safe filter must have balanced parentheses
    assert is_valid_ldap_filter_structure(safe_filter), (
        f"Safe filter has unbalanced parentheses for payload: {repr(payload)}\n"
        f"Filter: {safe_filter}"
    )

    # INVARIANT 2: The user-supplied value portion in the safe filter must not
    # contain unescaped LDAP metacharacters
    # Extract the user value portion from the safe filter
    # The safe filter wraps the escaped value, so we check the escaped value directly
    escaped_value = escape_ldap_filter_value(payload)
    assert not contains_unescaped_ldap_metacharacters(escaped_value), (
        f"Escaped value still contains unescaped metacharacters for payload: {repr(payload)}\n"
        f"Escaped: {escaped_value}"
    )

    # INVARIANT 3: If the payload contains injection characters, the safe and
    # vulnerable filters should differ (proving escaping actually happened)
    injection_chars = set('*()\x00')
    if any(c in payload for c in injection_chars):
        assert safe_filter != vulnerable_filter, (
            f"Safe filter should differ from vulnerable filter when payload contains "
            f"metacharacters, but they are identical for payload: {repr(payload)}"
        )

    # INVARIANT 4: The safe filter must not allow additional filter components
    # beyond what was intended - count top-level filter expressions
    # A properly constructed filter (& base_filter escaped_value) should have
    # exactly the structure we expect
    if payload and base_filter:
        # The safe filter should start with the expected prefix
        assert safe_filter.startswith("(& "), (
            f"Safe filter does not have expected structure for payload: {repr(payload)}\n"
            f"Filter: {safe_filter}"
        )

    # INVARIANT 5: Null bytes must be escaped in the output
    if '\x00' in payload:
        assert '\x00' not in safe_filter, (
            f"Null byte found in safe filter for payload: {repr(payload)}"
        )

    # INVARIANT 6: Raw unescaped parentheses from user input must not appear
    # in positions that could alter filter logic
    # After escaping, parentheses should be represented as \28 and \29
    if '(' in payload or ')' in payload:
        assert '\\28' in escaped_value or '\\29' in escaped_value, (
            f"Parentheses in payload were not escaped for payload: {repr(payload)}\n"
            f"Escaped value: {escaped_value}"
        )

    # INVARIANT 7: Wildcards from user input must be escaped
    if '*' in payload:
        assert '\\2a' in escaped_value, (
            f"Asterisk in payload was not escaped for payload: {repr(payload)}\n"
            f"Escaped value: {escaped_value}"
        )