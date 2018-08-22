#include "Uploader.h"
#include <iostream>
#include <random>
#include <chrono>
#include "Cache.h"

using namespace std;
using namespace l_blob_adapter;
using namespace chrono_literals;

int wmain(int argc, wchar_t *argv[]) {
	wcout << -1%10;
	auto uid = utility::new_uuid();
	auto pf = BasicFile::create(uid);
	pf->resize(100);
	return 0;
}