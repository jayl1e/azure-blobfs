#pragma once
#ifndef MYUTILS_H
#define MYUTILS_H


#include <string>
#include <cpprest/details/basic_types.h>
#include <log4cplus/logger.h>
#include <log4cplus/loggingmacros.h>
#include <cwctype>
#include <algorithm>

#if defined (LOG4CPLUS_MACRO_FUNCTION)
#undef LOG4CPLUS_MACRO_FUNCTION
#define LOG4CPLUS_MACRO_FUNCTION() __FUNCTION__
#endif

extern log4cplus::Logger g_logger;
#define LOG_FATAL(logEvent)  LOG4CPLUS_FATAL(g_logger, logEvent)
#define LOG_ERROR(logEvent)  LOG4CPLUS_ERROR(g_logger,logEvent)
#define LOG_WARN(logEvent)   LOG4CPLUS_WARN(g_logger, logEvent)
#define LOG_INFO(logEvent)   LOG4CPLUS_INFO(g_logger, logEvent)
#define LOG_DEBUG(logEvent)  LOG4CPLUS_DEBUG(g_logger, logEvent)
#define LOG_TRACE(logEvent)  LOG4CPLUS_TRACE(g_logger, logEvent)

#define TRACE_STUB log4cplus::TraceLogger __trace_log(g_logger, LOG4CPLUS_TEXT(""),__FILE__,__LINE__,__FUNCSIG__);

#define TRACE_LOG(logEvent) \
	LOG4CPLUS_MACRO_INSTANTIATE_OSTRINGSTREAM (___log4cplus_buf); \
	___log4cplus_buf << logEvent;                                 \
	log4cplus::TraceLogger ___trace_log(g_logger,___log4cplus_buf.str() ,__FILE__,__LINE__,__FUNCTION__)


std::string wstring2string(const std::wstring& ws);
std::wstring string2wstring(const std::string& as);

// trim from start (in place)
static inline void ltrim(std::wstring &s) {
	s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](int ch) {
		return !std::iswspace(ch);
	}));
	}

// trim from end (in place)
static inline void rtrim(std::wstring &s) {
	s.erase(std::find_if(s.rbegin(), s.rend(), [](int ch) {
		return !std::iswspace(ch);
	}).base(), s.end());
}

// trim from both ends (in place)
static inline void trim(std::wstring &s) {
	ltrim(s);
	rtrim(s);
}

#ifdef _UTF16_STRINGS
#define my_to_string(x) std::to_wstring(x)
#else
#define my_to_string(x) std::to_string(x)
#endif


#endif // !myutils
