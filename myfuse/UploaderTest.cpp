
#include <iostream>
#include <random>
#include <chrono>
#include <was/auth.h>
#include <was/storage_account.h>
#include "File.h"
#include "Uploader.h"

using namespace std;
using namespace l_blob_adapter;
using namespace chrono_literals;

azure::storage::cloud_blob_container azure_blob_container;


struct HashFunction {
	size_t operator () (const guid_t& uid)
	{
		return 0;
	}
};

int wmain(int argc, wchar_t *argv[]) {

	Uploader::run();
	utility::string_t storage_connection_string(U("DefaultEndpointsProtocol=http"));
	storage_connection_string += U(";AccountName=");
	storage_connection_string += U("mystorageaccount27052");
	storage_connection_string += U(";AccountKey=");
	storage_connection_string += U("t14kVApN3SL7CKAz6WWj5ELz2iSM51pDzcr4MBP2Rqa7urEqDF/E7fIuGHkwIQkKCYUDKDoFF/G8deLsFU2zKA==");

	azure::storage::cloud_storage_account storage_account = azure::storage::cloud_storage_account::parse(storage_connection_string);

	// Create the blob client.
	azure::storage::cloud_blob_client blob_client = storage_account.create_cloud_blob_client();

	// Retrieve a reference to a previously created container.
	azure_blob_container = blob_client.get_container_reference(L"mystoragecontainer");
	

	auto uid = utility::string_to_uuid(_XPLATSTR("bf2c3ffc-f2e1-489f-b98f-35bbc3f35399"));
	auto uid2 = utility::string_to_uuid(_XPLATSTR("bf2c3ffc-f2e1-489f-b98f-35bbc3f35389"));
	auto uid3 = utility::string_to_uuid(_XPLATSTR("bf2c3ffc-f2e1-489f-b98f-35bbc3f35379"));
	//auto pf = BasicFile::create(uid,FileType::F_Directory, azure_blob_container);
	
	auto& cache = BlockCache::instance()->cache;
	

	CommonFile* nf;
	//auto t = [&nf,uid,uid2,uid3]() {
	//
	//	nf=FileMap::instance()->create_dir(uid3);
	//	auto rf = nf->to_dir();
	//	/*guid_t cguid=rf->find(_XPLATSTR("testfile"));
	//	wcout << utility::uuid_to_string(cguid)<<endl;
	//	cout<<rf->rmEntry(_XPLATSTR("testfile"))<<endl;*/
	//	rf->addEntry(_XPLATSTR("testdir"), uid2);
	//	this_thread::sleep_for(3s);
	//	rf->addEntry(_XPLATSTR("testfile"), uid);
	//};
	//thread t1(t);
	/*auto pf = FileMap::instance()->get(uid);
	if (pf->get_type() == FileType::F_Regular) {

		auto reg = pf->to_reg();
		size_t readsize = 1 << 20;
		reg->trancate(5);
		uint8_t * buf = new uint8_t[readsize];
		uint8_t towrite[] = "new hello world how are you";
		auto readcnts = reg->read(0, readsize, buf);
		reg->write(3, sizeof(towrite), towrite);
	}*/

	guid_t findid = parse_path_relative(L"testfile", uid3);
	wcout << utility::uuid_to_string(findid) << endl;
	
	//pf->dec_nlink();
	//t1.join();
	Uploader::stop();
	return 0;
}
