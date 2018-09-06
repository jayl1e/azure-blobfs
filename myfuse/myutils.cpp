#define _SILENCE_ALL_CXX17_DEPRECATION_WARNINGS
#include "myutils.h"
#include <stdio.h>
#include <varargs.h>
#include <codecvt>
#include <chrono>
#include <sys/timeb.h>
#include <mutex>

static int __log_base_level = LOG_DEBUG;
static std::string __log_ident;

void setlogmask(int logupto) {
	__log_base_level = logupto;
}

std::mutex log_mutex;

void syslog(int level, const char * format, ...) {
	std::lock_guard lock(log_mutex);
	_timeb timebuffer;
	char timeline[26];
	errno_t err;
	_ftime_s(&timebuffer);
	err = ctime_s(timeline, 26, &(timebuffer.time));
	if (err) {
		printf_s("invalid argument to ctime_s");
	}
	
	va_list args;
	va_start(args, format);
	va_arg(args, const char *);
	if (level <= __log_base_level) {
		printf_s("%.15s.%03d : ", timeline + 4, timebuffer.millitm);
		printf_s(__log_ident.c_str());
		vprintf(format, args);
		printf("\n");
	}
}

void openlog(const char * log_ident, int flag, int facility) {
	__log_ident = log_ident;
	__log_ident += " : ";
}
void closelog() {

}

std::string wstring2string(const std::wstring& ws) {
	std::wstring_convert<std::codecvt_utf8<wchar_t> >conv;
	return conv.to_bytes(ws);
}

std::wstring string2wstring(const std::string& as) {
	std::wstring_convert<std::codecvt_utf8<wchar_t> >conv;
	return conv.from_bytes(as);
}