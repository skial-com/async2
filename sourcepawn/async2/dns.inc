#if defined _async2_dns_included
    #endinput
#endif
#define _async2_dns_included

// ============================================================================
// DNS Cache Natives
// ============================================================================

/**
 * Sets the c-ares DNS resolution timeout for TCP/UDP sockets.
 * Takes effect asynchronously (applied on the event thread).
 * Does not affect HTTP requests (curl manages its own DNS).
 * Default: 5000ms, 2 tries.
 *
 * @param timeout_ms    Timeout per DNS query attempt in milliseconds.
 * @param tries         Number of attempts before giving up.
 */
native void async2_DnsSetTimeout(int timeout_ms, int tries = 2);

/**
 * Flushes the application-level DNS cache.
 * All cached hostname entries are removed immediately.
 * Does not affect in-flight DNS queries.
 */
native void async2_DnsCacheFlush();

/**
 * Returns DNS cache statistics.
 *
 * @param entries   Number of cached hostname entries.
 * @param memory    Estimated memory usage in bytes.
 */
native void async2_DnsCacheStats(int &entries, int &memory);

/**
 * Sets the maximum TTL for DNS cache entries in seconds.
 * DNS responses with higher TTL are clamped to this value.
 * Set to 0 to disable caching (all lookups go to the network).
 * Default: 60 seconds.
 *
 * @param seconds   Maximum cache TTL in seconds.
 */
native void async2_DnsCacheSetTtl(int seconds);
