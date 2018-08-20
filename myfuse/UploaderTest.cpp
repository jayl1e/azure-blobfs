#include "Uploader.h"
#include <iostream>
#include <random>
#include <chrono>

using namespace std;
using namespace l_blob_adapter;
using namespace chrono_literals;

int wmain(int argc, wchar_t *argv[]) {
	Uploader::run();
	for (int i = 0; i < 100; i++) {
		Uploader::add_to_wait(i + 1);
		std::this_thread::sleep_for(100ms);
	}
	Uploader::stop();
	return 0;
}