
#include <iostream>
#include <random>
#include <chrono>
#include <was/auth.h>
#include <was/storage_account.h>
#include "BasicFile.h"
#include "Uploader.h"

using namespace std;
using namespace l_blob_adapter;
using namespace chrono_literals;

class UniqueTest {
public:
	virtual ~UniqueTest() {
		wcout << "destructor runs"<<endl;
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
	auto azure_blob_container = blob_client.get_container_reference(L"mystoragecontainer");
	

	auto uid = utility::string_to_uuid(_XPLATSTR("bf2c3ffc-f2e1-489f-b98f-35bbc3f35388"));
	auto pf = BasicFile::get(uid, azure_blob_container);

	auto& cache = BlockCache::instance()->cache;
	
	size_t readsize = 1<<22;
	uint8_t * buf = new uint8_t[readsize];
	uint8_t towrite[] = "hello world how are you";
	auto readcnts=pf->read_bytes(0, readsize, buf);
	pf->write_bytes(0, sizeof(towrite), towrite);
	pf->resize((1<<23) -1);
	
	Uploader::stop();
	return 0;
}