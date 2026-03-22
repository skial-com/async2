#if defined _async2_hjson_included
    #endinput
#endif
#define _async2_hjson_included

// ============================================================================
// HJSON Natives
// ============================================================================
// HJSON is a user-friendly JSON format that supports:
//   - Comments (// line and /* block */)
//   - Unquoted keys and string values
//   - Multiline strings (''')
//   - Trailing commas
//   - Optional root braces (root-level key: value pairs)
//
// Returns a Json handle — use the same Json methodmap and async2_JsonClose to work with it.

// Parses an HJSON string into a Json handle.
// Returns a new Json handle that must be closed with async2_JsonClose, or 0 on failure.
native Json async2_HjsonParseString(const char[] str);

// Parses an HJSON file (path relative to game directory) into a Json handle.
// Returns a new Json handle that must be closed with async2_JsonClose, or 0 on failure.
native Json async2_HjsonParseFile(const char[] path);
