#pragma once
#include "BasicFile.h"
#include <fuse.h>

namespace l_blob_adapter {

	class Directory;

	class CommonFile :public BasicFile {
		std::shared_mutex f_mutex;
		unique_ptr<CommonFile> get(guid_t guid);
		int azs_getattr(struct FUSE_STAT * stbuf);
		Directory* to_dir();
		Directory* to_reg();
	};


	struct DirEntry {
		wchar_t name[100];
		guid_t identifier;
	};
	class Directory :public CommonFile {
	public:
		int azs_readdir(void *buf, fuse_fill_dir_t filler, FUSE_OFF_T, struct fuse_file_info *);
		guid_t find(const string_t& name);
		void addEntry(const string_t name, guid_t identifier);
		void rmEntry(const string_t& name);
		static unique_ptr<CommonFile> create(guid_t guid);

	};

	class RegularFile :public CommonFile {
	public:
		size_t write(const pos_t offset, const size_t size, const uint8_t * buf);
		size_t read(const pos_t offset, const size_t size, uint8_t * buf);
		size_t trancate(pos_t off);
		static unique_ptr<RegularFile> create(guid_t guid);
	};

	class FileMap {
		unordered_map<guid_t, unique_ptr<CommonFile> > filemap;
	public:
		CommonFile * get(guid_t guid);
	};
}

