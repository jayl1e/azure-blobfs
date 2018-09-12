#pragma once
#include <vector>
#include <was/blob.h>
#include <memory>
#include <string>
#include <shared_mutex>


namespace l_blob_adapter {
	//guid_t means unique id (uuid)
	using guid_t = utility::uuid;
	using std::ptrdiff_t;
	using pos_t = std::ptrdiff_t;
	using std::size_t;
	using std::uint8_t;
	using std::unique_ptr;
	using std::shared_ptr;
	using std::wstring;
	using std::mutex;
	using std::shared_mutex;
	using std::shared_timed_mutex;
	using azure::storage::cloud_page_blob;
	using azure::storage::cloud_block_blob;
	using std::unordered_map;
	using std::vector;
	using utility::string_t;

	const size_t default_cache_size = 5000;
	const size_t default_block_size = 1<<20;
	const size_t max_parral_uploading_size = 50;
	const size_t uploader_min_latency_millisec = 2000;
}