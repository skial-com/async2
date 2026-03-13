#if defined _async2_time_included
    #endinput
#endif
#define _async2_time_included

// ============================================================================
// Time Natives
// ============================================================================

/**
 * Returns the current time as Unix epoch milliseconds (int64).
 *
 * Uses an anchored monotonic clock: monotonically increasing (never jumps
 * backwards), self-correcting against wall clock drift every 10 seconds.
 * Compatible with JS Date.now() and database timestamps.
 *
 * The result is written as int64 in little-endian int[2] format:
 *   result[0] = low 32 bits
 *   result[1] = high 32 bits
 *
 * @param result    Array to receive the epoch milliseconds.
 */
native void async2_GetTime(int result[2]);

/**
 * Monotonic epoch timestamp (milliseconds since Unix epoch as int64).
 *
 * All math is pure SourcePawn — no additional natives needed.
 * int64 is stored as two 32-bit cells: low (bits 0-31) and high (bits 32-63).
 */
enum struct Timestamp {
    int low;
    int high;

    /**
     * Sets this timestamp to the current time.
     */
    void Now() {
        int buf[2];
        async2_GetTime(buf);
        this.low = buf[0];
        this.high = buf[1];
    }

    /**
     * Returns milliseconds elapsed since this timestamp.
     * Valid for durations under ~24.8 days (fits in signed 32-bit).
     * Returns the low 32 bits of the difference.
     *
     * @return  Elapsed milliseconds.
     */
    int ElapsedMs() {
        int now[2];
        async2_GetTime(now);
        // 64-bit subtract: (now - this), return low word only
        // (valid for durations < 2^31 ms ≈ 24.8 days)
        return now[0] - this.low;
    }

    /**
     * Compares this timestamp to another.
     *
     * @param other     Timestamp to compare against.
     * @return          -1 if this < other, 0 if equal, 1 if this > other.
     */
    int Compare(Timestamp other) {
        // Compare high words first (signed)
        if (this.high < other.high) return -1;
        if (this.high > other.high) return 1;
        // High equal — compare low words as unsigned via XOR trick
        int a = this.low ^ 0x80000000;
        int b = other.low ^ 0x80000000;
        if (a < b) return -1;
        if (a > b) return 1;
        return 0;
    }

    /**
     * Returns seconds elapsed since this timestamp.
     * Valid for durations under ~68 years (fits in signed 32-bit seconds).
     *
     * @return  Elapsed seconds.
     */
    int ElapsedSeconds() {
        int now[2];
        async2_GetTime(now);
        // 64-bit subtract: (now - this) in ms, then divide by 1000
        int lo = now[0] - this.low;
        int borrow = 0;
        if ((now[0] ^ 0x80000000) < (this.low ^ 0x80000000)) {
            borrow = 1;
        }
        int hi = now[1] - this.high - borrow;
        // Divide int64 (hi:lo) by 1000 — only need the low word of the result
        // since seconds fit in 32 bits for durations < 68 years.
        // hi * (2^32 / 1000) gives the upper contribution in seconds.
        // lo / 1000 gives the lower contribution (treating lo as unsigned).
        int lo_unsigned_div;
        if (lo < 0) {
            // lo is "negative" in signed but represents a large unsigned value.
            // Split: (lo + 2^31) / 1000 + 2^31 / 1000
            // 2^31 / 1000 = 2147483 remainder 648
            lo_unsigned_div = (lo - (-2147483648)) / 1000 + 2147483;
            // Account for remainder: (lo & 0x7FFFFFFF) includes the 648 remainder
            // from 2^31/1000, but integer division absorbs it. Close enough for seconds.
        } else {
            lo_unsigned_div = lo / 1000;
        }
        return hi * 4294967 + lo_unsigned_div;
    }

    /**
     * Adds milliseconds to this timestamp. ms can be negative.
     *
     * @param ms    Milliseconds to add (positive or negative).
     */
    void AddMs(int ms) {
        int old_low = this.low;
        this.low += ms;
        if (ms >= 0) {
            // Positive add: carry if result wrapped (unsigned less than old)
            if ((this.low ^ 0x80000000) < (old_low ^ 0x80000000)) {
                this.high += 1;
            }
        } else {
            // Negative add: borrow if result wrapped (unsigned greater than old)
            if ((this.low ^ 0x80000000) > (old_low ^ 0x80000000)) {
                this.high -= 1;
            }
        }
    }

    /**
     * Adds seconds to this timestamp. seconds can be negative.
     *
     * @param seconds   Seconds to add (positive or negative).
     */
    void AddSeconds(int seconds) {
        // seconds * 1000 overflows int32 at ~24.8 days, so do the multiply
        // as int64: add (seconds / 1000) to high, remainder * 1000 to low.
        // But simpler: just add in two steps to avoid overflow.
        // 1000000 * 1000 = 10^9 which fits in int32, so split at 1000000s (~11.5 days).
        while (seconds > 1000000) {
            this.AddMs(1000000000); // 10^9 ms = 10^6 s
            seconds -= 1000000;
        }
        while (seconds < -1000000) {
            this.AddMs(-1000000000);
            seconds += 1000000;
        }
        this.AddMs(seconds * 1000);
    }

    /**
     * Subtracts another timestamp from this one: result = this - other.
     *
     * @param other     Timestamp to subtract.
     * @param result    Output: the difference as a Timestamp.
     */
    void Sub(Timestamp other, Timestamp result) {
        result.low = this.low - other.low;
        int borrow = 0;
        if ((this.low ^ 0x80000000) < (other.low ^ 0x80000000)) {
            borrow = 1;
        }
        result.high = this.high - other.high - borrow;
    }

    /**
     * Copies this timestamp to an int[2] array (for JSON int64 interop).
     *
     * @param result    Array to receive [low, high].
     */
    void ToInt64(int result[2]) {
        result[0] = this.low;
        result[1] = this.high;
    }

}
