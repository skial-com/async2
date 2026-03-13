#if defined _async2_url_included
    #endinput
#endif
#define _async2_url_included

// ============================================================================
// URL / Query String Natives
// ============================================================================

/**
 * URL-encodes a string (percent-encoding per RFC 3986).
 * Letters, digits, '_', '.', '-', '~' are left unencoded. All others → %XX.
 *
 * @param input   Null-terminated input string.
 * @param output  Output buffer for the encoded string.
 * @param maxlen  Output buffer size.
 * @return        Number of characters written, or -1 if buffer too small.
 */
native int async2_UrlEncode(const char[] input, char[] output, int maxlen);

/**
 * URL-decodes a percent-encoded string.
 * Converts %XX sequences and '+' signs back to their original characters.
 *
 * @param input   Null-terminated percent-encoded string.
 * @param output  Output buffer for the decoded string.
 * @param maxlen  Output buffer size.
 * @return        Number of bytes written, or -1 if buffer too small.
 */
native int async2_UrlDecode(const char[] input, char[] output, int maxlen);

/**
 * Parses a URL query string into a JSON object.
 * Keys and values are URL-decoded. A leading '?' is stripped if present.
 * PHP-style "key[]" brackets are stripped from key names.
 * Duplicate keys are automatically promoted to arrays:
 *   "a=1"           → {"a": "1"}
 *   "a=1&a=2"       → {"a": ["1", "2"]}
 *   "a[]=1&a[]=2"   → {"a": ["1", "2"]}
 * Keys without '=' get empty string values.
 *
 * @param queryString  e.g. "foo=bar&baz=hello%20world" or "?foo=bar&baz=qux"
 * @return             New Json handle (object), or 0 on failure.
 *                     Must be closed with async2_JsonClose.
 */
native Json async2_QueryStringParse(const char[] queryString);

/**
 * Builds a URL query string from a JSON object.
 * Keys and values are URL-encoded. Values are converted to strings:
 * string as-is, int/float to decimal, bool to "true"/"false", null/other skipped.
 * Array values emit repeated keys: {"a": ["1","2"]} → "a=1&a=2".
 *
 * @param jsonHandle  JSON object handle.
 * @param output      Output buffer for the query string (no leading '?').
 * @param maxlen      Output buffer size.
 * @return            Number of characters written, or -1 on error (not an object, buffer too small).
 */
native int async2_QueryStringBuild(Json jsonHandle, char[] output, int maxlen);
