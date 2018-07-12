#include <was/storage_account.h>
#include <was/blob.h>
#include <cpprest/filestream.h>
#include <cpprest/containerstream.h>
#include <cstring>
#include <vector>
#include <cstdint>
#include <fuse.h>
#include <iostream>

using std::string;
using std::cout;
using std::exception;

int main(int argc, char * argv[]) {
	wchar_t s1[] = L"hello你好";
	char s2[] = "hello你好";
	cout << s1;
	return 0;
}