#pragma once
#include <was/storage_account.h>
#include <was/blob.h>
#include <cpprest/filestream.h>  
#include <cpprest/containerstream.h>
#include "blobfuse.h"
#include <map>


using namespace concurrency;
using namespace azure;
using namespace std;

class BlobProxy
{
public:
	BlobProxy(const utility::string_t& _name, bool _managed, unique_ptr<storage::cloud_block_blob>&& _pblob);
	size_t read(char * buf, size_t size, FUSE_OFF_T offset, struct fuse_file_info * fi);
	size_t write(const char *buf, size_t size, FUSE_OFF_T offset, struct fuse_file_info *fi);
	size_t truncate(FUSE_OFF_T offset);
	static BlobProxy* access(const utility::string_t& name);
	bool close();
	int getattr(struct FUSE_STAT * stbuf);
	bool exist();
	bool sync();
	virtual ~BlobProxy();

private:
	class Iterator;
	class ConstIterator;
	class RawIterator;

	int m_exist;
	bool managed;
	std::unique_ptr<storage::cloud_block_blob> pblob;
	utility::string_t name;
	std::deque<uint8_t> buffer;
	size_t blocksize;
	size_t totalsize;
	std::map<int, vector<uint8_t> > blocks;
	std::map<int, storage::block_list_item> blocklists;

	vector<uint8_t>& get_block(int block_id, bool writable);
	ConstIterator get_c_iter(size_t pos);

	ConstIterator c_end();


	class RawIterator {
	public:
		RawIterator(BlobProxy& proxy, size_t position, bool writebale);
		inline bool operator==(const BlobProxy::RawIterator & other);
		using char_t = uint8_t;
		inline RawIterator& operator++();
		bool is_writable;
	protected:
		BlobProxy & m_base;
		std::size_t m_position;
		vector<uint8_t> * m_pblock;
		std::size_t m_next_block_pos;
		std::size_t m_this_block_pos;
	};

	class Iterator :public RawIterator {
	public:
		Iterator(BlobProxy& proxy, size_t position):RawIterator(proxy,position,true){}
		inline char_t& operator*();
	};

	class ConstIterator :public RawIterator {
	public:
		ConstIterator(BlobProxy& proxy, size_t position) :RawIterator(proxy, position,false) {}
		inline char_t operator*();
	};


};

