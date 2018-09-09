#define _CRT_SECURE_NO_WARNINGS
#define _SILENCE_ALL_CXX17_DEPRECATION_WARNINGS
#include <was/storage_account.h>
#include <was/blob.h>
#include <cpprest/filestream.h>  
#include <cpprest/containerstream.h>
#include <cpprest/producerconsumerstream.h>
#include <codecvt>

#include <log4cplus/loglevel.h>
#include <log4cplus/logger.h>
#include <log4cplus/fileappender.h> 
#include <log4cplus/consoleappender.h> 
#include <log4cplus/layout.h>
#include <log4cplus/loggingmacros.h>
#include <log4cplus/initializer.h>

#include "BlobAdapter.h"

using std::wstring;
using std::string;
using std::wcout;
using std::cout;
using std::exception;

extern int read_and_set_arguments(int argc, char *argv[], struct fuse_args *args);
extern int validate_storage_connection();
extern void configure_fuse(struct fuse_args *args);
extern void set_up_callbacks();
extern struct fuse_operations azs_fuse_operations;

using namespace concurrency;

int wmain(int argc, wchar_t * argv[], wchar_t * envp[]) {
	log4cplus::Initializer initializer;
	log4cplus::SharedAppenderPtr pConsoleAppender(new log4cplus::ConsoleAppender());
	pConsoleAppender->setLayout(std::make_unique<log4cplus::TTCCLayout>());
	auto pTestLogger = log4cplus::Logger::getInstance(L"");
	pTestLogger.setLogLevel(log4cplus::TRACE_LOG_LEVEL);
	pTestLogger.addAppender(pConsoleAppender);
	
	
	char ** cargv = new char*[argc];

	for (int i = 0; i < argc; i++) {
		int r=WideCharToMultiByte(CP_UTF8, 0, argv[i], 0,nullptr, 0, NULL, NULL);
		std::wstring_convert<std::codecvt_utf8<wchar_t> >conv;
		string str= conv.to_bytes(argv[i]);
		cargv[i] = _strdup(str.c_str());
	}
	struct fuse_args args;
	int ret = read_and_set_arguments(argc, cargv, &args);
	if (ret != 0)
	{
		return ret;
	}

	//ret = validate_storage_connection();
	//if (ret != 0)
	//{
	//	return ret;
	//}

	configure_fuse(&args);

	set_up_callbacks();

	auto& cache = BlockCache::instance()->cache;

	ret = fuse_main(args.argc, args.argv, &azs_fuse_operations, NULL);

	return 0;
}
