#ifndef _WT_SYSTEM_MONITOR_H
#define _WT_SYSTEM_MONITOR_H
#define WT_SYSTEM_MONITOR

#ifdef WT_SYSTEM_MONITOR
#define WT_BOOT_REASON
#define WT_BOOTLOADER_LOG
#include "wt_boot_reason.h"
#include "wt_bootloader_log_save.h"
#endif

#endif
