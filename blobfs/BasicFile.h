#pragma once
#include "Common.h"
#include "Block.h"

namespace l_blob_adapter {
	
	

	class Block;
	class Synchornizor;

	enum class FileType
	{
		F_Regular,
		F_Directory
	};


	class Snapshot;
	class BlockBlobUploadHelper;
	

	class BasicFile
	{
	public:
		BasicFile();
		virtual ~BasicFile();

		static unique_ptr<BasicFile> get(guid_t file_identifier, const azure::storage::cloud_blob_container& container);
		static unique_ptr<BasicFile> create(guid_t file_identifier, FileType type, const azure::storage::cloud_blob_container& container);
		unique_ptr<Snapshot> create_snap(); 

		guid_t get_id() { return m_file_identifier; }
		inline FileType type() { return this->m_type; };
		inline bool exist() { return m_exist; };
		size_t write_bytes(const pos_t offset, const size_t size, const uint8_t * buf);
		size_t read_bytes(const pos_t offset, const size_t size, uint8_t * buf);
		size_t set_meta(const string_t& key, const string_t& val); //lock file
		string_t get_meta(const string_t& key);
		size_t resize(size_t size);//lock file
		void inc_nlink();//lock file 
		void dec_nlink();//lock file
		int64_t get_nlink() { return this->nlink; }
		size_t get_blockcnt() { return this->filesize ? (this->filesize - 1) / this->blocksize : 0; }
		size_t get_blocksize() { return this->blocksize; }
		size_t get_filesize() { return this->filesize; }
	protected:

		guid_t m_file_identifier;
		unique_ptr<cloud_block_blob> m_pblob;
		std::mutex blob_mutex;

		shared_timed_mutex m_mutex;
		std::mutex up_mutex;
		ItemStatus status;


		const Block* get_read_block(const size_t blockindex); //lock file
		Block* get_write_block(const size_t blockindex); //lock file
		Block* get_write_block_copy(const size_t blockindex); //lock file

		

		//file block chain
		vector<pos_t> blocklist;
		std::mutex block_mutex;

		//property
		size_t blocksize;
		size_t filesize;
		bool m_exist;
		FileType m_type;
		int64_t nlink;

		//metadata
		unordered_map<string_t, string_t> metadata;
		azure::storage::cloud_blob_properties properties;
		
		friend class Snapshot;
		friend class BlockBlobUploadHelper;
		friend class Block;
	};

	class Snapshot {
	public:
		vector<azure::storage::block_list_item> blocklist;
		unordered_map<pos_t, pos_t> dirtyblock;
		azure::storage::cloud_metadata metadata;
		azure::storage::cloud_blob_properties properties;
		BasicFile* basefile;
		std::unique_lock<std::mutex> uplock;
		Snapshot(BasicFile& file, std::unique_lock<std::mutex> && uplock);
		virtual ~Snapshot();
	};

}


