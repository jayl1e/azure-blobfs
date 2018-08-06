#include <was/storage_account.h>
#include <was/blob.h>
#include <cpprest/filestream.h>  
#include <cpprest/containerstream.h>
#include <cpprest/producerconsumerstream.h>
#include <codecvt>

#include "blobfuse.h"

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

	vector<int> a = { 1,2,3 };

	char ** cargv = new char*[argc];

	for (int i = 0; i < argc; i++) {
		int r=WideCharToMultiByte(CP_UTF8, 0, argv[i], 0,nullptr, 0, NULL, NULL);
		std::wstring_convert<std::codecvt_utf8<wchar_t> >conv;
		string str= conv.to_bytes(argv[i]);
		cargv[i] = strdup(str.c_str());
	}
	struct fuse_args args;
	int ret = read_and_set_arguments(argc, cargv, &args);
	if (ret != 0)
	{
		return ret;
	}

	ret = validate_storage_connection();
	if (ret != 0)
	{
		return ret;
	}

	configure_fuse(&args);

	set_up_callbacks();


	ret = fuse_main(args.argc, args.argv, &azs_fuse_operations, NULL);

	return 0;
}
