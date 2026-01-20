#ifndef WAIT_SIGNAL_BITS_H
#define WAIT_SIGNAL_BITS_H



/*
 * System-wide synchronization bits
 * Each bit represents a specific system-level event or status.
 * These bits are shared across all components.
 */

// Wi-Fi related events

#define SYNC_EVENT_DISCOVERY_COMPLETE     (1 << 0)  // Bit 0: Discovery process completed
// Add more as needed (up to 24–32 bits recommended per group)




#endif