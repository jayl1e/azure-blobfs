#pragma once

#include <vector>
#include "BasicFile.h"


namespace l_blob_adapter {

	using std::vector;

	class Dirent {
		guid_t uid;
		wstring name;
	};

	class DirectoryFile:public BasicFile
	{
	public:
		DirectoryFile();
		virtual ~DirectoryFile();

		vector<Dirent> readdir();
	};
}