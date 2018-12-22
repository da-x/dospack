#define DP_GLOBAL_FACILITY   DP_LOG_FACILITY_LOG
#define DP_LOGGING           (logging)

#include "logging.h"

#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <sys/time.h>
#include <string.h>

#define ANSI_RESET_COLORS        "\033[0m"
#define ANSI_RED_BOLD_COLOR      "\033[1;31m"
#define ANSI_GREEN_BOLD_COLOR    "\033[1;32m"
#define ANSI_YELLOW_BOLD_COLOR   "\033[1;33m"
#define ANSI_BLUE_BOLD_COLOR     "\033[1;34m"
#define ANSI_MAGENTA_BOLD_COLOR  "\033[1;35m"
#define ANSI_CYAN_BOLD_COLOR     "\033[1;36m"
#define ANSI_WHITE_BOLD_COLOR    "\033[1;37m"
#define ANSI_DARK_GRAY_COLOR     "\033[1;30m"
#define ANSI_RED_COLOR           "\033[0;31m"
#define ANSI_GREEN_COLOR         "\033[0;32m"
#define ANSI_YELLOW_COLOR        "\033[0;33m"
#define ANSI_BLUE_COLOR          "\033[0;34m"
#define ANSI_MAGENTA_COLOR       "\033[0;35m"
#define ANSI_CYAN_COLOR          "\033[0;36m"
#define ANSI_GRAY_COLOR          "\033[0;37m"

#define X(name, str) [DP_LOG_FACILITY_##name] = str,
static const char *dp_fac_str[] = {
	DP_LOG_FACILITIES_X
};

#undef X

#define X(name, str) [DP_LOG_LEVEL_##name] = str,
static const char *dp_lev_str[] = {
	DP_LOG_LEVELS_X
};

#undef X

void dp_log_init(struct dp_logging *logging, const char *level_spec)
{
	int i, j, k;

	for (i = 0; i < DP_LOG_FACILITY__NUM; i++)
		logging->facility_level[i] = DP_LOG_LEVEL_INFO;

	for (i = -1; i <= DP_LOG_FACILITY__NUM; i++) {
		const char *fac_str = NULL;

		if (i == DP_LOG_FACILITY__NUM)
			fac_str = "all";
		else if (i == -1)
			fac_str = "default";
		else
			fac_str = dp_fac_str[i];

		if (level_spec) {
			char buf[0x20];
			const char *h, *p;

			snprintf(buf, sizeof(buf), "%s=", fac_str);
			h = strstr(level_spec, buf);
			if (h != NULL) {
				h += strlen(buf);
				p = strstr(h, ",");
				if (!p)
					p = h + strlen(h);

				if (p - h < sizeof(buf) - 1) {
					int found = 0;
					memcpy(buf, h, p - h);
					buf[p - h] = '\0';

					for (j = 0; j < DP_LOG_LEVELS__NUM; j++) {
						if (!strcmp(dp_lev_str[j], buf)) {

							DP_INF("changing logging level of %s to %s", fac_str,
							       dp_lev_str[j]);

							if (i == DP_LOG_FACILITY__NUM ||
							    i == -1) {
								for (k=0; k < DP_LOG_FACILITY__NUM; k++)
									logging->facility_level[k] = j;
							} else {
								logging->facility_level[i] = j;
							}

							found = 1;
							break;
						}
					}

					if (!found) {
						DP_WRN("invalid logging level of %s passed to %s", buf, fac_str);
					}

				}
			}
		}
	}
}

void dp_log_set_machine_time_cb(struct dp_logging *logging, dp_log_get_machine_time_t func, void *ptr)
{
	logging->get_machine_time_cb.func = func;
	logging->get_machine_time_cb.ptr = ptr;
}

void dp_log(struct dp_logging *logging, enum dp_log_level level, enum dp_log_facility log_facility, const char *fmt,
	    ...)
{
	const char *strlevel = NULL;
	const char *stransifacility = ANSI_RESET_COLORS;
	const char *stransilevel = "";
	struct timeval tv;
	struct tm tm;
	va_list ap;
	u64 timetrack = 0;

	strlevel = dp_lev_str[level];

	switch (level) {
	case DP_LOG_LEVEL_DEBUG:
		stransilevel = ANSI_GREEN_COLOR;
		break;
	case DP_LOG_LEVEL_INFO:
		stransilevel = ANSI_WHITE_BOLD_COLOR;
		break;
	case DP_LOG_LEVEL_NOTICE:
		stransilevel = ANSI_CYAN_BOLD_COLOR;
		break;
	case DP_LOG_LEVEL_WARNING:
		stransilevel = ANSI_YELLOW_BOLD_COLOR;
		break;
	case DP_LOG_LEVEL_ERROR:
		stransilevel = ANSI_RED_BOLD_COLOR;
		break;
	case DP_LOG_LEVEL_FATAL:
		stransilevel = ANSI_MAGENTA_BOLD_COLOR;
		break;
	default:
		break;
	};

	gettimeofday(&tv, NULL);
	gmtime_r(&tv.tv_sec, &tm);

	if (logging->get_machine_time_cb.func) {
		timetrack = logging->get_machine_time_cb.func(logging->get_machine_time_cb.ptr);
	}

	printf(ANSI_BLUE_BOLD_COLOR
	       "%02d:%02d:%02d.%06d" ANSI_DARK_GRAY_COLOR
	       " | %s%s" ANSI_DARK_GRAY_COLOR
	       " | %s%-4s" ANSI_DARK_GRAY_COLOR
	       " |" ANSI_CYAN_COLOR " %012llu" ANSI_DARK_GRAY_COLOR
	       " | %s",
	       tm.tm_hour, tm.tm_min, tm.tm_sec, (unsigned int)tv.tv_usec, stransilevel, strlevel,
	       stransifacility, dp_fac_str[log_facility], timetrack, stransilevel);

	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);

	printf(ANSI_RESET_COLORS "\n");
}

#undef DP_GLOBAL_FACILITY
#undef DP_LOGGING
