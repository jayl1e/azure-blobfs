#include "Uploader.h"

using namespace l_blob_adapter;

Uploader * l_blob_adapter::Uploader::get_instance()
{
	if (instance == nullptr) {
		std::lock_guard<std::mutex> lock(s_mutex);
		if (instance == nullptr) {
			instance = new Uploader();
		}
	}
	return instance;
}

Uploader::Uploader()
{
}


Uploader::~Uploader()
{
}
