#define _SILENCE_ALL_CXX17_DEPRECATION_WARNINGS
#include "myutils.h"
#include <stdio.h>
#include <varargs.h>
#include <codecvt>
#include <chrono>
#include <sys/timeb.h>
#include <mutex>
#include <thread>
#include <iomanip>
#include <log4cplus/loglevel.h>
#include <log4cplus/logger.h>
#include <log4cplus/loggingmacros.h>

static int __log_base_level = LOG_DEBUG;
static std::string __log_ident;

void setlogmask(int logupto) {
	__log_base_level = logupto;
}

std::mutex log_mutex;

void syslog(int level, const char * format, ...) {
	va_list args;
	va_start(args, format);
	va_arg(args, const char *);
	auto logger = log4cplus::Logger::getInstance(L"lijie");
	LOG4CPLUS_DEBUG(logger, format);
	return;

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
		std::ostringstream str;
		str.write(timeline + 4, 15);
		str << '.' << std::setw(3) <<std::setfill('0')<< timebuffer.millitm;
		str << ": thread " << std::this_thread::get_id()<<" : ";
		printf_s("%s", str.str().c_str());
		vprintf(format, args);
		printf("\n");
	}
	va_end(args);
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