// ============================================================================
// Timestamp tests
// ============================================================================

void Test_Time_Now() {
    Timestamp t;
    t.Now();

    // Epoch ms as of 2026 should have high word ~405 (1.74 trillion / 2^32)
    Assert(t.high > 0, "Time_Now: high > 0");
    Assert(t.high >= 300 && t.high <= 600, "Time_Now: high in expected range");
    PrintToServer("  Timestamp.Now() = high:%d low:%d", t.high, t.low);
}

void Test_Time_Monotonic() {
    Timestamp t1, t2;
    t1.Now();
    t2.Now();

    int cmp = t2.Compare(t1);
    Assert(cmp >= 0, "Time_Monotonic: second >= first");
}

void Test_Time_ElapsedMs() {
    Timestamp t;
    t.Now();

    // ElapsedMs right after should be >= 0
    int elapsed = t.ElapsedMs();
    Assert(elapsed >= 0, "Time_ElapsedMs: >= 0 immediately");
}

void Test_Time_ElapsedSeconds() {
    Timestamp t;
    t.Now();

    int elapsed = t.ElapsedSeconds();
    AssertEq(elapsed, 0, "Time_ElapsedSeconds: 0 immediately");

    // Subtract 5500ms manually, check ElapsedSeconds rounds down to 5
    t.AddMs(-5500);
    elapsed = t.ElapsedSeconds();
    Assert(elapsed >= 5 && elapsed <= 6, "Time_ElapsedSeconds: ~5s after subtracting 5500ms");
}

void Test_Time_Compare() {
    Timestamp a, b;
    a.low = 100;
    a.high = 1;
    b.low = 200;
    b.high = 1;

    AssertEq(a.Compare(b), -1, "Time_Compare: a < b same high");
    AssertEq(b.Compare(a), 1, "Time_Compare: b > a same high");
    AssertEq(a.Compare(a), 0, "Time_Compare: a == a");

    // Different high
    Timestamp c;
    c.low = 0;
    c.high = 2;
    AssertEq(a.Compare(c), -1, "Time_Compare: lower high < higher high");
    AssertEq(c.Compare(a), 1, "Time_Compare: higher high > lower high");
}

void Test_Time_AddMs() {
    Timestamp t;
    t.low = 100;
    t.high = 5;

    t.AddMs(200);
    AssertEq(t.low, 300, "Time_AddMs: simple add low");
    AssertEq(t.high, 5, "Time_AddMs: simple add high unchanged");

    // Add negative
    t.AddMs(-100);
    AssertEq(t.low, 200, "Time_AddMs: negative add low");
    AssertEq(t.high, 5, "Time_AddMs: negative add high unchanged");
}

void Test_Time_AddMs_Carry() {
    Timestamp t;
    t.low = 0x7FFFFFFF;  // max signed int
    t.high = 5;

    t.AddMs(1);
    // 0x7FFFFFFF + 1 = 0x80000000 (wraps in unsigned sense)
    AssertEq(t.low, 0x80000000, "Time_AddMs_Carry: low wrapped");
    AssertEq(t.high, 5, "Time_AddMs_Carry: no carry (no unsigned wrap)");

    // Now add 0x7FFFFFFF more to cause an unsigned carry
    t.low = -1;  // 0xFFFFFFFF unsigned
    t.high = 5;
    t.AddMs(1);
    // 0xFFFFFFFF + 1 = 0x00000000 with carry
    AssertEq(t.low, 0, "Time_AddMs_Carry: low wrapped to 0");
    AssertEq(t.high, 6, "Time_AddMs_Carry: high incremented on carry");
}

void Test_Time_AddSeconds() {
    Timestamp t;
    t.low = 100;
    t.high = 5;

    t.AddSeconds(3);
    AssertEq(t.low, 3100, "Time_AddSeconds: 3s = 3000ms added");
    AssertEq(t.high, 5, "Time_AddSeconds: high unchanged");

    // Negative
    t.AddSeconds(-1);
    AssertEq(t.low, 2100, "Time_AddSeconds: -1s = -1000ms");
    AssertEq(t.high, 5, "Time_AddSeconds: high unchanged after neg");
}

void Test_Time_AddSeconds_Large() {
    // Test > 24.8 days (> 2,147,483 seconds) to verify overflow handling
    Timestamp t;
    t.Now();

    int orig_low = t.low;
    int orig_high = t.high;

    // Add 30 days then subtract 30 days
    t.AddSeconds(2592000);   // 30 days in seconds
    t.AddSeconds(-2592000);

    AssertEq(t.low, orig_low, "Time_AddSeconds_Large: round-trip low");
    AssertEq(t.high, orig_high, "Time_AddSeconds_Large: round-trip high");
}

void Test_Time_Sub() {
    Timestamp a, b;
    a.low = 500;
    a.high = 10;
    b.low = 200;
    b.high = 10;

    Timestamp result;
    a.Sub(b, result);
    AssertEq(result.low, 300, "Time_Sub: simple sub low");
    AssertEq(result.high, 0, "Time_Sub: simple sub high");
}

void Test_Time_Sub_Borrow() {
    Timestamp a, b;
    a.low = 100;
    a.high = 10;
    b.low = 200;
    b.high = 10;

    Timestamp result;
    a.Sub(b, result);
    AssertEq(result.low, -100, "Time_Sub_Borrow: low with borrow");
    AssertEq(result.high, -1, "Time_Sub_Borrow: high decremented");
}

void Test_Time_ToInt64() {
    Timestamp t;
    t.low = 42;
    t.high = 99;

    int buf[2];
    t.ToInt64(buf);
    AssertEq(buf[0], 42, "Time_ToInt64: low");
    AssertEq(buf[1], 99, "Time_ToInt64: high");
}

void Test_Time_RoundTrip_AddSub() {
    Timestamp t;
    t.Now();

    int orig_low = t.low;
    int orig_high = t.high;

    t.AddMs(5000);   // +5 seconds
    t.AddMs(-5000);  // -5 seconds

    AssertEq(t.low, orig_low, "Time_RoundTrip: low unchanged after +5000/-5000");
    AssertEq(t.high, orig_high, "Time_RoundTrip: high unchanged after +5000/-5000");
}

void RunTimeTests() {
    Test_Time_Now();
    Test_Time_Monotonic();
    Test_Time_ElapsedMs();
    Test_Time_ElapsedSeconds();
    Test_Time_Compare();
    Test_Time_AddMs();
    Test_Time_AddMs_Carry();
    Test_Time_AddSeconds();
    Test_Time_AddSeconds_Large();
    Test_Time_Sub();
    Test_Time_Sub_Borrow();
    Test_Time_ToInt64();
    Test_Time_RoundTrip_AddSub();
}
