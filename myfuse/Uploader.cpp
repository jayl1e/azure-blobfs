#include "Uploader.h"
#include "myutils.h"
#include <was/core.h>
#include <thread>
#include "BasicFile.h"

using namespace l_blob_adapter;
using namespace std::chrono_literals;

void l_blob_adapter::Uploader::run()
{
	auto t=std::thread([]() {get_instance()->run_upload(); });
	t.detach();
}

void l_blob_adapter::Uploader::stop()
{
	Uploader * instance = get_instance();
	std::unique_lock<std::mutex> lock(instance->stop_mutex);
	instance->stop_flag.store(true);
	instance->stop_condition.wait(lock);
	return;
}

void l_blob_adapter::Uploader::wait()
{
	Uploader * instance = get_instance();
	std::unique_lock<std::mutex> lock(instance->stop_mutex);
	instance->stop_condition.wait(lock);
}

void l_blob_adapter::Uploader::add_to_wait(pos_t pos)
{
	Uploader::get_instance()->add_to_queue(pos);
}

void l_blob_adapter::Uploader::add_to_queue(pos_t pos)
{
	auto now = std::chrono::system_clock::now();
	std::lock_guard<std::mutex> guard(queue_mutex);
	upload_queue.emplace(pos, now);
}

Uploader * l_blob_adapter::Uploader::get_instance()
{
	if (instance == nullptr) {
		std::lock_guard<std::mutex> lock(s_mutex);
		if (instance == nullptr) {
			instance = new Uploader();
			instance->helper = unique_ptr<UploadHelper>(std::make_unique<BlockBlobUploadHelper>());
		}
	}
	return instance;
}

Uploader::Uploader()
{
}

void l_blob_adapter::Uploader::run_upload()
{
	while (true)
	{

#ifdef DEBUG
		std::wcout << L"queue size: " << upload_queue.size() << std::endl;
#endif // DEBUG

		bool is_empty;
		UploadItem item;
		{
			std::lock_guard<std::mutex> guard(queue_mutex);
			is_empty = upload_queue.empty();
			if (!is_empty) {
				item = upload_queue.front();
			}
			else {
				if (stop_flag) {
					stop_condition.notify_one();
					return;
				}
			}
		}
		if (is_empty) {
			std::this_thread::sleep_for(std::chrono::milliseconds(this->timeout_in_milisecond));
			continue;
		}
		auto now = std::chrono::system_clock::now();
		auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(now - item.time);
		if (milliseconds.count() > this->timeout_in_milisecond) {
			pplx::task<int> t = helper->generate_task(item.pos);
			{
				std::lock_guard<std::mutex> guard(queue_mutex);
				upload_queue.pop();
			}
			int res = t.get();
			if (res > 0) {
				add_to_queue(item.pos);
			}
		}
		else {
			std::this_thread::sleep_for(std::chrono::milliseconds(this->timeout_in_milisecond));
		}
	}
}

std::mutex Uploader::s_mutex;
Uploader* Uploader::instance=nullptr;
extern int validate_storage_connection();

pplx::task<int> l_blob_adapter::BlockBlobUploadHelper::generate_task(pos_t pos)
{
	return pplx::task<int>([pos]() {
		try {
			BasicFile* pfile=((BasicFile*)(pos));//Todo get file ref
			unique_ptr<Snapshot> snap = pfile->create_snap();
			auto basefile = snap->basefile;
			vector<concurrency::task<void>> tasks;

			LOG_DEBUG("uploading file=" << utility::uuid_to_string(basefile->get_id()));

			if (!pfile->exist()) {
				basefile->m_pblob->delete_blob_if_exists();
				LOG_DEBUG("uploader delete file=" << utility::uuid_to_string(basefile->get_id()));
				return 0;
			}
			
			size_t dirty_cnt = 0;
			
			for (pos_t i = 0; i < snap->blocklist.size() ; i++) {
				if (snap->blocklist.at(i).mode() == azure::storage::block_list_item::uncommitted) {
					auto t=basefile->m_pblob->upload_block_async(snap->blocklist.at(i).id(), 
						concurrency::streams::container_stream<vector<uint8_t>>::open_istream(BlockCache::get(snap->dirtyblock[i])->data), L"");
					tasks.emplace_back(std::move(t));
					dirty_cnt++;
				}
				if (tasks.size() > max_parral_uploading_size) {
					concurrency::when_all(std::begin(tasks), std::end(tasks)).wait();
					tasks.resize(0);
				}
			}
			LOG_DEBUG("uploader file=" << utility::uuid_to_string(basefile->get_id()) << ", dirty blocks=" << dirty_cnt);
			concurrency::when_all(std::begin(tasks), std::end(tasks)).wait();
			tasks.resize(0);
			basefile->m_pblob->upload_block_list(snap->blocklist);
			std::lock_guard<std::mutex> guard(basefile->blob_mutex);
			basefile->m_pblob->metadata() = snap->metadata;
			//basefile->m_pblob->properties() = snap->properties;
			tasks.emplace_back(basefile->m_pblob->upload_metadata_async());
			//tasks.emplace_back(basefile->m_pblob->upload_properties_async());
			concurrency::when_all(std::begin(tasks), std::end(tasks)).wait();
		}
		catch (azure::storage::storage_exception& e) {
			if (e.retryable()) {
				std::wcerr << L"error happen to be retried: " <<e.what()<< std::endl;
				return 1;
			}
			else {
				return -1;
			}
		}
		catch (...) {
			std::wcerr << L"critical error" << std::endl;
			auto ptr = std::current_exception();
			return -1;
		}
		return 0; });
}
