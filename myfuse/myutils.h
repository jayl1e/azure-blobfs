#pragma once
#ifndef MYUTILS_H
#define MYUTILS_H

#include <string>
#include <cpprest/details/basic_types.h>

#define LOG_EMERG 0
#define LOG_ALERT 1
#define LOG_CRIT 2
#define LOG_ERR 3
#define LOG_WARNING 4
#define LOG_NOTICE 5
#define LOG_INFO 6
#define LOG_DEBUG 7

#define LOG_UPTO(x) x

void setlogmask(int logupto);
void syslog(int level, const char * format, ...);
void openlog(const char * log_ident,int flag, int facility);
void closelog();
std::string wstring2string(const std::wstring& ws);
std::wstring string2wstring(const std::string& as);


#ifdef _UTF16_STRINGS
#define my_to_string(x) std::to_wstring(x)
#else
#define my_to_string(x) std::to_string(x)
#endif


#endif // !myutils
