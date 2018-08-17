#pragma once

#include "BasicFile.h"
#include <queue>
#include <future>

namespace l_blob_adapter {

	class UploadHelper {

	};

	class Uploader
	{
	public:
		static void add_to_wait(pos_t pos);
		Uploader* get_instance();

	private:
		Uploader();
		void run_sync();
		std::queue<pos_t> uploading_queue;
		std::mutex queue_mutex;

		std::mutex s_mutex;
		Uploader* instance;
	};
}


