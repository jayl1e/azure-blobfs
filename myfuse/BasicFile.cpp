#include "BasicFile.h"
#include <string>
#include "myutils.h"


using namespace l_blob_adapter;

BasicFile::BasicFile()
{
}

l_blob_adapter::BasicFile::~BasicFile()
{
}



unique_ptr<Snapshot> l_blob_adapter::BasicFile::create_snap()
{
	std::unique_lock<std::mutex> uplock(this->up_mutex);
	std::unique_lock<std::shared_timed_mutex> lock(this->m_mutex);
	return std::make_unique<Snapshot>(*this,std::move(uplock));
}

const Block & l_blob_adapter::BasicFile::get_read_block(const size_t blockindex)
{
	std::lock_guard guard(m_mutex);
	pos_t &pos = blocklist.at(blockindex);
	if ( pos> 0) {
		return Blocks::block_cache.at(pos);
	}
	else {
		auto buffer = concurrency::streams::container_buffer< vector<uint8_t> >();
		buffer.set_buffer_size(this->blocksize, std::ios_base::out);
		auto ostream = concurrency::streams::ostream(buffer);
		m_pblob->download_range_to_stream(ostream, blockindex*this->blocksize, this->blocksize);
		// may be GC
		//pos_t blockpos=Blocks::get_free_block();
		pos_t blockpos = 0;

		std::lock_guard blockguard(Blocks::block_cache.at(blockpos).mut);
		Blocks::block_cache.at(blockpos).data.assign(buffer.collection().begin(), buffer.collection().end());
		Blocks::block_cache.at(blockpos).status = BlockStatus::B_Clean;
		this->blocklist.at(blockindex) = blockpos;
		return Blocks::block_cache.at(blockpos);
	}
}

Block & l_blob_adapter::BasicFile::get_write_block(const size_t blockindex)
{
	std::lock_guard guard(m_mutex);
	pos_t &pos = blocklist.at(blockindex);
	if (pos> 0) {
		return Blocks::block_cache.at(pos);
	}
	else {
		pos_t blockpos = 0;// Blocks::get_free_block();
		std::lock_guard blockguard(Blocks::block_cache.at(blockpos).mut);
		Blocks::block_cache.at(blockpos).status = BlockStatus::B_Dirty;
		this->blocklist.at(blockindex) = blockpos;
		return Blocks::block_cache.at(blockpos);
	}
}


l_blob_adapter::Snapshot::Snapshot(BasicFile & file, std::unique_lock<std::mutex> && uplock)
{
	this->uplock = std::move(uplock);
	this->basefile = file.shared_from_this();
	this->metadata = file.metadata;
	this->metadata[_XPLATSTR("l_filesize")] = file.filesize;
	this->metadata[_XPLATSTR("l_blocksize")] = file.blocksize;
	this->properties = file.properties;
	for (pos_t index = 0; index < file.blocklist.size();index++) {
		if (file.blocklist.at(index) > 0) {
			Block& block = Blocks::block_cache[file.blocklist.at(index)];
			std::lock_guard blockguard(block.mut);
			if (block.status == BlockStatus::B_Dirty) {
				block.status = BlockStatus::B_Uploading;
				this->dirtyblock[index] = file.blocklist.at(index);
				this->blocklist.emplace_back( my_to_string(index) , azure::storage::block_list_item::uncommitted );
			}
			else {
				this->blocklist.emplace_back(my_to_string(index), azure::storage::block_list_item::committed);
			}
		}
		else {
			this->blocklist.emplace_back(my_to_string(index), azure::storage::block_list_item::committed);
		}
	}

}

l_blob_adapter::Snapshot::~Snapshot()
{
	std::unique_lock<std::shared_timed_mutex> lock(basefile->m_mutex);
	if (basefile->status == FileStatus::F_Uploading) {
		for (auto& pair : dirtyblock) {
			//restore blocks
			auto& block = Blocks::block_cache.at(pair.second);
			std::lock_guard blockguard(block.mut);
			if (block.status == BlockStatus::B_Uploading)block.status = BlockStatus::B_Clean;
		}
		basefile->status = FileStatus::F_Clean;
	}
	else if(basefile->status == FileStatus::F_Dirty){
		for (auto& pair : dirtyblock) {
			//restore blocks and maybe gc
			auto& block = Blocks::block_cache.at(pair.second);
			std::lock_guard blockguard(block.mut);
			if (block.status == BlockStatus::B_Uploading)block.status = BlockStatus::B_Clean;
			else if (block.status == BlockStatus::B_Up_Expired)block.status = BlockStatus::B_Expired;
		}
	}
}
