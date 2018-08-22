#pragma once

#include "BasicFile.h"
#include "Block.h"
#include <queue>
#include <future>
#include <chrono>
#include <condition_variable>
#include <atomic>


namespace l_blob_adapter {

	class UploadHelper {
	public:
		// return int to indentify the result stauts, 0 for sucess
		virtual pplx::task<int> generate_task(pos_t pos)=0;
	};

	class BlockBlobUploadHelper:public UploadHelper {
		pplx::task<int> generate_task(pos_t pos);
	};

	struct UploadItem {
		pos_t pos;
		std::chrono::time_point<std::chrono::system_clock> time;
		UploadItem() {}
		UploadItem(pos_t p, std::chrono::time_point<std::chrono::system_clock> t) :pos(p), time(t) {}
	};

	class Uploader
	{
	public:
		static void run();
		static void stop();
		void run_upload();
		static void add_to_wait(pos_t pos);
		static Uploader* get_instance();
		
		long long timeout_in_milisecond = 1000;

	private:
		Uploader();
		void add_to_queue(pos_t pos);
		std::queue<UploadItem> upload_queue;
		std::mutex queue_mutex;
		unique_ptr<UploadHelper> helper;

		std::condition_variable stop_condition;
		std::mutex stop_mutex;
		std::atomic_bool stop_flag=false;

		static std::mutex s_mutex;
		static Uploader* instance;
	};
}


