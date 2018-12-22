#ifndef _DOSPACK_LOGGING_H__
#define _DOSPACK_LOGGING_H__

#include "common.h"

#define DP_LOG_LEVELS_X			\
	X(DEBUG,       "dbg")			\
	X(INFO,        "inf")			\
	X(NOTICE,      "ntc")			\
	X(WARNING,     "wrn")			\
	X(ERROR,       "err")			\
	X(FATAL,       "fat")			\

#undef X

#define X(name, str) DP_LOG_LEVEL_##name,
enum dp_log_level {
	DP_LOG_LEVELS_X

	DP_LOG_LEVELS__NUM,
};
#undef X

#define DP_LOG_FACILITIES_X			\
	X(LOG,          "log" )		\
	X(MAIN,         "main")		\
	X(MEMORY,       "mem" )		\
	X(IO,           "io"  )		\
	X(CPU,          "cpu" )		\
	X(PAGING,       "page")		\
	X(CPU_DECODE,   "deco")		\
	X(CPU_DISASM,   "dism")		\
	X(DISPLAY,      "disp")		\
	X(PIC,          "pic" )		\
	X(FILES,        "file")		\
	X(DOSBLOCK,     "dosb")		\
	X(INT_CALLBACK, "calb")		\
	X(MARSHAL,      "mash")		\
	X(TIMETRACK,    "trak")		\
	X(BIOS,         "bios")		\
	X(VIDEO,        "vid")			\
	X(KEYBOARD,     "keyb")		\
	X(HWTIMER,      "hwtm")		\
	X(EVENTS,       "evnt")		\
	X(GAME,         "game")		\
	X(UI,           "ui  ")		\

#define X(name, str) DP_LOG_FACILITY_##name,
enum dp_log_facility {
	DP_LOG_FACILITIES_X

	DP_LOG_FACILITY__NUM,
};
#undef X

typedef u64 (*dp_log_get_machine_time_t)(void *ptr);

struct dp_logging {
	unsigned char facility_level[DP_LOG_FACILITY__NUM];

	struct {
		dp_log_get_machine_time_t func;
		void *ptr;
	} get_machine_time_cb;

	void (*abort)(void);
};

#define PRECOMPILED_LEVEL_FILTER     DP_LOG_LEVEL_DEBUG
#define PRECOMPILED_FACILITY_FILTER  0

void dp_log_init(struct dp_logging *logging, const char *level_spec);
void dp_log_set_machine_time_cb(struct dp_logging *logging, dp_log_get_machine_time_t func, void *ptr);
void dp_log(struct dp_logging *logging, enum dp_log_level level, enum dp_log_facility log_facility, const char *fmt, ...);

#define DP_FILTERED_OP(_level_, facility, OP) do {			\
		if (PRECOMPILED_LEVEL_FILTER <= _level_) { 				\
			if (DP_LOGGING->facility_level[facility] <= _level_) { 	\
				OP;					\
			}								\
		}									\
	} while (0);									\

#define DP_LOGF(_level_, facility, fmt, args...) do {					\
		DP_FILTERED_OP(_level_, facility, dp_log(DP_LOGGING, _level_, facility, fmt, ##args)); \
		if (DP_LOG_LEVEL_FATAL == _level_) {					\
			DP_LOGGING->abort();						\
		}									\
	} while (0);									\

#define DP_LOGI(_level_, facility, fmt, args...) DP_LOGF(DP_LOG_LEVEL_##_level_, DP_LOG_FACILITY_##facility, fmt, ##args)

#define DP_ASSERT(_cond_) do { \
		int cond = _cond_;     \
		if (!cond) {	       \
			DP_FAT("assert '%s' failed in %s:%d", #_cond_, __FUNCTION__, __LINE__); \
		}		       \
	} while (0);		       \

#define DP_ASSERTF(_cond_, fmt, args...) do {  \
		int cond = _cond_;     \
		if (!cond) {	       \
			DP_FAT("assert '%s' failed in %s:%d: " fmt, #_cond_, __FUNCTION__, __LINE__, ##args); \
		}		       \
	} while (0);		       \

#define DP_LOG(level, fmt, args...) \
	DP_LOGF(level, DP_GLOBAL_FACILITY, fmt, ##args)
#define DP_DBG(fmt, args...) \
	DP_LOG(DP_LOG_LEVEL_DEBUG, fmt, ##args)
#define DP_INF(fmt, args...) \
	DP_LOG(DP_LOG_LEVEL_INFO, fmt, ##args)
#define DP_NTC(fmt, args...) \
	DP_LOG(DP_LOG_LEVEL_NOTICE, fmt, ##args)
#define DP_WRN(fmt, args...) \
	DP_LOG(DP_LOG_LEVEL_WARNING, fmt, ##args)
#define DP_ERR(fmt, args...) \
	DP_LOG(DP_LOG_LEVEL_ERROR, fmt, ##args)
#define DP_FAT(fmt, args...) \
	DP_LOG(DP_LOG_LEVEL_FATAL, fmt, ##args)

#define DP_TRC(level, fmt, args...) \
	DP_LOG(level, "%s() " fmt, __FUNCTION__, ##args)

#define DP_TRC_DBG(fmt, args...) DP_TRC(DP_LOG_LEVEL_DEBUG, fmt, ##args)
#define DP_TRC_INF(fmt, args...) DP_TRC(DP_LOG_LEVEL_INFO, fmt, ##args)
#define DP_TRC_WRN(fmt, args...) DP_TRC(DP_LOG_LEVEL_WARNING, fmt, ##args)

#define DP_UNIMPLEMENTED_FAT \
	DP_FAT("%s:%d: not implemented", __FUNCTION__, __LINE__)

#endif
