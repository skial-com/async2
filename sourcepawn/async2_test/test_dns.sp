// DNS Cache Tests (synchronous)

void Test_DNS_CacheStats() {
    int entries, memory;
    async2_DnsCacheStats(entries, memory);
    Assert(entries >= 0, "DnsCacheStats entries >= 0");
    Assert(memory >= 0, "DnsCacheStats memory >= 0");
}

void Test_DNS_CacheFlush() {
    async2_DnsCacheFlush();
    int entries, memory;
    async2_DnsCacheStats(entries, memory);
    AssertEq(entries, 0, "DnsCacheFlush empties cache");
}

void Test_DNS_CacheSetTtl() {
    // Set TTL, verify no crash
    async2_DnsCacheSetTtl(120);
    g_passed++;

    // Set to 0 (disable), verify no crash
    async2_DnsCacheSetTtl(0);
    g_passed++;

    // Restore default
    async2_DnsCacheSetTtl(60);
    g_passed++;
}

void Test_DNS_SetTimeout() {
    // Set timeout, verify no crash (applied async on event thread)
    async2_DnsSetTimeout(3000, 1);
    g_passed++;

    // Restore default
    async2_DnsSetTimeout(5000, 2);
    g_passed++;
}

void RunDnsTests() {
    Test_DNS_CacheStats();
    Test_DNS_CacheFlush();
    Test_DNS_CacheSetTtl();
    Test_DNS_SetTimeout();
}
