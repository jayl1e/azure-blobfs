#pragma once
#include "BasicFile.h"
#include <fuse.h>
#include <errno.h>

extern azure::storage::cloud_blob_container azure_blob_container;

namespace l_blob_adapter {

	class Directory;
	class RegularFile;

	class CommonFile {
	public:
		unique_ptr<BasicFile> basicfile;
		std::shared_mutex f_mutex;
		static unique_ptr<CommonFile> get(guid_t guid, const azure::storage::cloud_blob_container& container);
		int azs_getattr(struct FUSE_STAT * stbuf);
		bool exist() { return basicfile!=nullptr && basicfile->exist(); }

		FileType get_type() { return basicfile->type(); }
		Directory* to_dir() { if (basicfile->type() == FileType::F_Directory) return (Directory*)(this); else return nullptr; };
		RegularFile* to_reg() { if (basicfile->type() == FileType::F_Regular) return (RegularFile*)(this); else return nullptr; };
	};


	struct DirEntry {
		wchar_t name[100];
		guid_t identifier;
	};
	class Directory :public CommonFile {
	public:
		int azs_readdir(void *buf, fuse_fill_dir_t filler, FUSE_OFF_T, struct fuse_file_info *);
		guid_t find(const string_t& name);
		bool addEntry(const string_t name, guid_t identifier);
		bool rmEntry(const string_t& name);
		static unique_ptr<CommonFile> create(guid_t guid, const azure::storage::cloud_blob_container& container);
	};

	class RegularFile :public CommonFile {
	public:
		size_t write(const pos_t offset, const size_t size, const uint8_t * buf);
		size_t read(const pos_t offset, const size_t size, uint8_t * buf);
		size_t trancate(pos_t off);
		static unique_ptr<RegularFile> create(guid_t guid, const azure::storage::cloud_blob_container& container);
	};

	struct GuidHasher {
		size_t operator()(const guid_t& guid) const {
			const uint64_t* half = reinterpret_cast<const uint64_t*>(&guid);
			return half[0] ^ half[1];
		}
	};

	class FileMap {
		unordered_map<guid_t, unique_ptr<CommonFile>, GuidHasher> filemap;
		std::mutex maplock;
		static FileMap* m_instance;
		static std::mutex instance_mutex;
	public:
		static FileMap* instance();
		CommonFile * get(guid_t guid);
		RegularFile* create_reg(guid_t guid);
		Directory* create_dir(guid_t guid);
	};

	guid_t parse_path_relative(string_t path, guid_t base);
}

