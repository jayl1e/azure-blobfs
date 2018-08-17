#pragma once
#include <shared_mutex>
#include <vector>
#include <memory>
#include <list>
#include <chrono>

namespace l_blob_adapter {
	using std::shared_mutex;
	using std::vector;
	using std::shared_ptr;
	using std::list;
	using std::weak_ptr;
	class BasicFile;

	enum class BlockStatus {
		B_Clean,
		B_Dirty,
		B_Uploading,
		B_Up_Expired,
		B_Expired
	};

	class Block {
	public:
		BlockStatus status;
		weak_ptr<BasicFile> file;
		shared_mutex mut;

		vector<uint8_t> data;
	};


	class BlockCache {

	};



	class Blocks
	{
	public:
		Blocks();
		virtual ~Blocks();

		static size_t get_free_block();

		static vector<Block> block_cache;
		static size_t max_cache;
		static list<size_t> freelist;
	};

}