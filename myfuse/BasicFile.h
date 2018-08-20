#pragma once
#include <memory>
#include <string>
#include <shared_mutex>
#include <unordered_map>
#include <was/blob.h>
#include <vector>
#include "Blocks.h"

namespace l_blob_adapter {
	//guid_t means unique id (uuid)
	using guid_t = utility::uuid;
	using std::ptrdiff_t;
	using pos_t= std::ptrdiff_t;
	using std::size_t;
	using std::uint8_t;
	using std::unique_ptr;
	using std::shared_ptr;
	using std::wstring;
	using std::shared_mutex;
	using std::shared_timed_mutex;
	using azure::storage::cloud_page_blob;
	using azure::storage::cloud_block_blob;
	using std::unordered_map;
	using std::vector;
	using utility::string_t;

	class Block;
	class Synchornizor;

	enum class FileType
	{
		F_Regular,
		F_Directory
	};

	enum class GcLevel
	{
		GC_Clean,
		GC_Dirty,
		GC_Old
	};

	enum class FileStatus {
		F_Clean,
		F_Dirty,
		F_Uploading,
	};

	class Snapshot;
	class BlockBlobUploadHelper;
	

	class BasicFile:public std::enable_shared_from_this<BasicFile>
	{
	public:
		BasicFile();
		virtual ~BasicFile();

		static unique_ptr<BasicFile> get(guid_t file_identifier);
		static unique_ptr<BasicFile> create(guid_t file_identifier);

		inline FileType type() { return this->m_type;};
		inline bool exist() { return m_exist; };
		unique_ptr<Snapshot> create_snap();

	protected:
		size_t write_bytes(const pos_t offset, const size_t size, const uint8_t * buf);
		size_t read_bytes(const pos_t offset, const size_t size, const uint8_t * buf); 
		size_t set_attr(const string_t& key, const string_t& val); //lock file
		size_t resize(size_t size);//lock file

	private:

		guid_t m_file_identifier;
		unique_ptr<cloud_block_blob> m_pblob;
		std::mutex blob_mutex;

		shared_timed_mutex m_mutex;
		std::mutex up_mutex;
		FileStatus status;


		const Block& get_read_block(const size_t blockindex); //lock file
		Block& get_write_block(const size_t blockindex); //lock file

		//file block chain
		vector<pos_t> blocklist;

		//property
		size_t blocksize;
		size_t filesize;
		size_t totalsize;
		bool m_exist;
		FileType m_type;

		//metadata
		unordered_map<string_t, string_t> metadata;
		azure::storage::cloud_blob_properties properties;
		
		friend class Snapshot;
		friend class BlockBlobUploadHelper;
	};

	class Snapshot {
	public:
		vector<azure::storage::block_list_item> blocklist;
		unordered_map<pos_t, pos_t> dirtyblock;
		azure::storage::cloud_metadata metadata;
		azure::storage::cloud_blob_properties properties;
		shared_ptr<BasicFile> basefile;
		std::unique_lock<std::mutex> uplock;
		Snapshot(BasicFile& file, std::unique_lock<std::mutex> && uplock);
		virtual ~Snapshot();
	};

}


