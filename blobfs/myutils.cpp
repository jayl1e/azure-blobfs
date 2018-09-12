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



log4cplus::Logger g_logger = log4cplus::Logger::getInstance(LOG4CPLUS_TEXT(""));



std::string wstring2string(const std::wstring& ws) {
	std::wstring_convert<std::codecvt_utf8<wchar_t> >conv;
	return conv.to_bytes(ws);
}

std::wstring string2wstring(const std::string& as) {
	std::wstring_convert<std::codecvt_utf8<wchar_t> >conv;
	return conv.from_bytes(as);
}