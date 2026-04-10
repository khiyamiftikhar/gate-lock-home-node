#pragma once
#include <stdbool.h>
#include "stdint.h"

/* Start periodic SD logging
 * interval_ms → how often to flush logs
 */
bool sd_log_writer_start(uint32_t interval_ms);

/* Stop logging (optional) */
void sd_log_writer_stop(void);