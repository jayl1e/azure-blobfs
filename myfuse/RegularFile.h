#pragma once

#include "BasicFile.h"

namespace l_blob_adapter {


	class RegularFile:public BasicFile
	{
	public:
		RegularFile();
		virtual ~RegularFile();

		int read(uint8_t * buf, ptrdiff_t offset, size_t size);
		int write(uint8_t * buf, ptrdiff_t offset, size_t size);
		int truncate(ptrdiff_t offset);
	};
}



