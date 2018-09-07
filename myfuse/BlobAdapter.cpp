#define _CRT_SECURE_NO_WARNINGS
#include <was/storage_account.h>
#include <was/blob.h>
#include <cpprest/filestream.h>  
#include <cpprest/containerstream.h>
#include <string>
#include <algorithm>
#include <cctype>
#include <exception>
#include "BlobAdapter.h"


struct Str_options str_options = {};
int file_cache_timeout_in_seconds = 30;
int default_permission = 0777;
struct fuse_operations azs_fuse_operations = {};
using std::wstring;
using std::string;
using namespace l_blob_adapter;

guid_t rootdir;

CommonFile * parse_path(const std::string& path)
{
	guid_t t = parse_path_relative(utility::conversions::to_string_t(path), rootdir);
	if (t == guid_t()) {
		return nullptr;
	}
	else {
		return FileMap::instance()->get(t);
	}
}

CommonFile * parse_path(const l_blob_adapter::string_t& path)
{
	guid_t t = parse_path_relative(path, rootdir);
	if (t == guid_t()) {
		return nullptr;
	}
	else {
		return FileMap::instance()->get(t);
	}
}

void split_path(std::string path, string_t& parentname, string_t& shortname) {
	if (path.back() == '/')path.pop_back();
	size_t pos = path.find_last_of('/');
	parentname = utility::conversions::to_string_t(path.substr(0, pos));
	shortname = utility::conversions::to_string_t(path.substr(pos + 1));
	trim(shortname);
}

// FUSE contains a specific type of command-line option parsing; here we are just following the pattern.
struct Options
{
	const char *tmp_path; // Path to the temp / file cache directory
	const char *config_file; // Connection to Azure Storage information (account name, account key, etc)
	const char *use_https; // True if https should be used (defaults to false)
	const char *file_cache_timeout_in_seconds; // Timeout for the file cache (defaults to 120 seconds)
	const char *container_name; //container to mount. Used only if config_file is not provided
	const char *log_level; // Sets the level at which the process should log to syslog.
	const char *version; // print blobfuse version
	const char *help; // print blobfuse usage
};

struct Options options;

#define OPTION(t, p) { t, offsetof(struct Options, p), 1 }
const struct fuse_opt option_spec[] =
{
	OPTION("--tmp-path=%s", tmp_path),
	OPTION("--config-file=%s", config_file),
	OPTION("--use-https=%s", use_https),
	OPTION("--file-cache-timeout-in-seconds=%s", file_cache_timeout_in_seconds),
	OPTION("--container-name=%s", container_name),
	OPTION("--log-level=%s", log_level),
	OPTION("--version", version),
	OPTION("-v", version),
	OPTION("--help", help),
	OPTION("-h", help),
	FUSE_OPT_END
};

std::shared_ptr<azure::storage::cloud_blob_container> azure_blob_container;


const std::string log_ident = "blobfuse";

void print_usage()
{
	std::weak_ptr<azure::storage::cloud_blob_container> wkptr = azure_blob_container;
	fprintf(stdout, "Usage: blobfuse <mount-folder> --tmp-path=</path/to/fusecache> [--config-file=</path/to/config.cfg> | --container-name=<containername>]");
	fprintf(stdout, "    [--use-https=true] [--file-cache-timeout-in-seconds=120] [--log-level=LOG_OFF|LOG_CRIT|LOG_ERR|LOG_WARNING|LOG_INFO|LOG_DEBUG]\n\n");
	fprintf(stdout, "In addition to setting --tmp-path parameter, you must also do one of the following:\n");
	fprintf(stdout, "1. Specify a config file (using --config-file]=) with account name, account key, and container name, OR\n");
	fprintf(stdout, "2. Set the environment variables AZURE_STORAGE_ACCOUNT and AZURE_STORAGE_ACCESS_KEY, and specify the container name with --container-name=\n\n");
	fprintf(stdout, "See https://github.com/Azure/azure-storage-fuse for detailed installation and configuration instructions.\n");
}

static std::string trim(const std::string& str) {
	const size_t start = str.find_first_not_of(' ');
	if (std::string::npos == start) {
		return std::string();
	}
	const size_t end = str.find_last_not_of(' ');
	return str.substr(start, end - start + 1);
}

int read_config_env()
{
	char* env_account = getenv("AZURE_STORAGE_ACCOUNT");
	char* env_account_key = getenv("AZURE_STORAGE_ACCESS_KEY");
	char* env_sas_token = getenv("AZURE_STORAGE_SAS_TOKEN");

	if (env_account)
	{
		str_options.accountName = env_account;
	}
	else
	{
		syslog(LOG_CRIT, "Unable to start blobfuse.  No config file was specified and AZURE_STORAGE_ACCESS_KEY environment variable is empty.");
		fprintf(stderr, "No config file was specified and AZURE_STORAGE_ACCOUNT environment variable is empty.\n");
		return -1;
	}

	if (env_account_key)
	{
		str_options.accountKey = env_account_key;
	}

	if (env_sas_token)
	{
		str_options.sasToken = env_sas_token;
	}

	if ((!env_account_key && !env_sas_token) ||
		(env_account_key && env_sas_token))
	{
		syslog(LOG_CRIT, "Unable to start blobfuse.  If no config file is specified, exactly one of the environment variables AZURE_STORAGE_ACCESS_KEY or AZURE_STORAGE_SAS_TOKEN must be set.");
		fprintf(stderr, "Unable to start blobfuse.  If no config file is specified, exactly one of the environment variables AZURE_STORAGE_ACCESS_KEY or AZURE_STORAGE_SAS_TOKEN must be set.\n");
	}

	return 0;
}

int read_config(const std::string configFile)
{
	std::ifstream file(configFile);
	if (!file)
	{
		syslog(LOG_CRIT, "Unable to start blobfuse.  No config file found at %s.", configFile.c_str());
		fprintf(stderr, "No config file found at %s.\n", configFile.c_str());
		return -1;
	}

	std::string line;
	std::istringstream data;

	while (std::getline(file, line))
	{

		data.str(line.substr(line.find(" ") + 1));
		const std::string value(trim(data.str()));

		if (line.find("accountName") != std::string::npos)
		{
			std::string accountNameStr(value);
			str_options.accountName = accountNameStr;
		}
		else if (line.find("accountKey") != std::string::npos)
		{
			std::string accountKeyStr(value);
			str_options.accountKey = accountKeyStr;
		}
		else if (line.find("sasToken") != std::string::npos)
		{
			std::string sasTokenStr(value);
			str_options.sasToken = sasTokenStr;
		}
		else if (line.find("containerName") != std::string::npos)
		{
			std::string containerNameStr(value);
			str_options.containerName = containerNameStr;
		}
		else if (line.find("blobEndpoint") != std::string::npos)
		{
			std::string blobEndpointStr(value);
			str_options.blobEndpoint = blobEndpointStr;
		}

		data.clear();
	}

	if (str_options.accountName.empty())
	{
		syslog(LOG_CRIT, "Unable to start blobfuse. Account name is missing in the config file.");
		fprintf(stderr, "Account name is missing in the config file.\n");
		return -1;
	}
	else if ((str_options.accountKey.empty() && str_options.sasToken.empty()) ||
		(!str_options.accountKey.empty() && !str_options.sasToken.empty()))
	{
		syslog(LOG_CRIT, "Unable to start blobfuse. Exactly one of Account Key and SAS token must be specified in the config file, and the other line should be deleted.");
		fprintf(stderr, "Unable to start blobfuse. Exactly one of Account Key and SAS token must be specified in the config file, and the other line should be deleted.\n");
		return -1;
	}
	else if (str_options.containerName.empty())
	{
		syslog(LOG_CRIT, "Unable to start blobfuse. Container name is missing in the config file.");
		fprintf(stderr, "Container name is missing in the config file.\n");
		return -1;
	}
	else
	{
		return 0;
	}
}



void print_version()
{
	fprintf(stdout, "blobfuse 1.0.2\n");
}


int set_log_mask(const char * min_log_level_char)
{
	if (!min_log_level_char)
	{
		setlogmask(LOG_UPTO(LOG_DEBUG));
		return 0;
	}
	std::string min_log_level(min_log_level_char);
	if (min_log_level.empty())
	{
		setlogmask(LOG_UPTO(LOG_WARNING));
		return 0;
	}
	// Options for logging: LOG_OFF, LOG_CRIT, LOG_ERR, LOG_WARNING, LOG_INFO, LOG_DEBUG
	if (min_log_level == "LOG_OFF")
	{
		setlogmask(LOG_UPTO(LOG_EMERG)); // We don't use 'LOG_EMERG', so this won't log anything.
		return 0;
	}
	if (min_log_level == "LOG_CRIT")
	{
		setlogmask(LOG_UPTO(LOG_CRIT));
		return 0;
	}
	if (min_log_level == "LOG_ERR")
	{
		setlogmask(LOG_UPTO(LOG_ERR));
		return 0;
	}
	if (min_log_level == "LOG_WARNING")
	{
		setlogmask(LOG_UPTO(LOG_WARNING));
		return 0;
	}
	if (min_log_level == "LOG_INFO")
	{
		setlogmask(LOG_UPTO(LOG_INFO));
		return 0;
	}
	if (min_log_level == "LOG_DEBUG")
	{
		setlogmask(LOG_UPTO(LOG_DEBUG));
		return 0;
	}

	syslog(LOG_CRIT, "Unable to start blobfuse. Error: Invalid log level \"%s\"", min_log_level.c_str());
	fprintf(stdout, "Error: Invalid log level \"%s\".  Permitted values are LOG_OFF, LOG_CRIT, LOG_ERR, LOG_WARNING, LOG_INFO, LOG_DEBUG.\n", min_log_level.c_str());
	fprintf(stdout, "If not specified, logging will default to LOG_WARNING.\n\n");
	return 1;
}

int read_and_set_arguments(int argc, char *argv[], struct fuse_args *args)
{
	// FUSE has a standard method of argument parsing, here we just follow the pattern.
	*args = FUSE_ARGS_INIT(argc, argv);

	// Check for existence of allow_other flag and change the default permissions based on that
	default_permission = 0777;
	std::vector<std::string> string_args(argv, argv + argc);
	for (size_t i = 1; i < string_args.size(); ++i) {
		if (string_args[i].find("allow_other") != std::string::npos) {
			default_permission = 0777;
		}
	}

	int ret = 0;
	try
	{

		if (fuse_opt_parse(args, &options, option_spec, NULL) == -1)
		{
			return 1;
		}

		if (options.version)
		{
			print_version();
			exit(0);
		}

		if (options.help)
		{
			print_usage();
			exit(0);
		}

		if (!options.config_file)
		{
			if (!options.container_name)
			{
				syslog(LOG_CRIT, "Unable to start blobfuse, no config file provided and --container-name is not set.");
				fprintf(stderr, "Error: No config file provided and --container-name is not set.\n");
				print_usage();
				return 1;
			}

			std::string container(options.container_name);
			str_options.containerName = container;
			ret = read_config_env();
		}
		else
		{
			ret = read_config(options.config_file);
		}

		if (ret != 0)
		{
			return ret;
		}
	}
	catch (std::exception &)
	{
		print_usage();
		return 1;
	}

	int res = set_log_mask(options.log_level);
	if (res != 0)
	{
		print_usage();
		return 1;
	}

	// remove last trailing slash in tmo_path
	if (!options.tmp_path)
	{
		fprintf(stderr, "Error: --tmp-path is not set.\n");
		print_usage();
		return 1;
	}

	std::string tmpPathStr(options.tmp_path);
	if (!tmpPathStr.empty() && tmpPathStr[tmpPathStr.size() - 1] == '/')
	{
		tmpPathStr.erase(tmpPathStr.size() - 1);
	}

	str_options.tmpPath = tmpPathStr;
	str_options.use_https = true;
	if (options.use_https != NULL)
	{
		std::string https(options.use_https);
		if (https == "false")
		{
			str_options.use_https = false;
		}
	}

	if (options.file_cache_timeout_in_seconds != NULL)
	{
		std::string timeout(options.file_cache_timeout_in_seconds);
		file_cache_timeout_in_seconds = stoi(timeout);
	}
	else
	{
		file_cache_timeout_in_seconds = 120;
	}
	return 0;
}


int validate_storage_connection()
{
	// The current implementation of blob_client_wrapper calls curl_global_init() in the constructor, and curl_global_cleanup in the destructor.
	// Unfortunately, curl_global_init() has to be called in the same process as any HTTPS calls that are made, otherwise NSS is not configured properly.
	// When running in daemon mode, the current process forks() and exits, while the child process lives on as a daemon.
	// So, here we create and destroy a temp blob client in order to test the connection info, and we create the real one in azs_init, which is called after the fork().
	{
		const int defaultMaxConcurrency = 20;
		utility::string_t storage_connection_string(U("DefaultEndpointsProtocol=http"));
		storage_connection_string += U(";AccountName=");
		storage_connection_string += U("mystorageaccount27053");
		storage_connection_string += U(";AccountKey=");
		storage_connection_string += U("t14kVApN3SL7CKAz6WWj5ELz2iSM51pDzcr4MBP2Rqa7urEqDF/E7fIuGHkwIQkKCYUDKDoFF/G8deLsFU2zKA==");

		azure::storage::cloud_storage_account storage_account = azure::storage::cloud_storage_account::parse(storage_connection_string);

		// Create the blob client.
		azure::storage::cloud_blob_client blob_client = storage_account.create_cloud_blob_client();

		// Retrieve a reference to a previously created container.
		auto azure_blob_container = blob_client.get_container_reference(string2wstring("mystoragecontainer"));
		auto blob = azure_blob_container.get_block_blob_reference(L"sample.mp4");
		vector<uint8_t> rawdata;
		rawdata.resize(0);
		concurrency::streams::istream fis;
		unordered_map<utility::string_t, utility::string_t> meta;
		try {
			blob.download_attributes();
			auto & me = blob.metadata();
			me[L"h"] = L"1";
			blob.upload_metadata();
			auto blocks = blob.download_block_list();
		}
		catch (...) {
			auto ptr = std::current_exception();
			try {
				std::rethrow_exception(ptr);
			}
			catch (azure::storage::storage_exception& e) {
				std::cerr << e.retryable() << e.what();
				throw e;
			}

		}


	}
	return 0;
}

void configure_fuse(struct fuse_args *args)
{
	fuse_opt_add_arg(args, "-omax_read=131072");

	fuse_opt_add_arg(args, "-f"); //forground window

	if (options.file_cache_timeout_in_seconds != NULL)
	{
		std::string timeout(options.file_cache_timeout_in_seconds);
		file_cache_timeout_in_seconds = stoi(timeout);
	}
	else
	{
		file_cache_timeout_in_seconds = 120;
	}
	//fuse_opt_add_arg(args, "-s");//disable multithread
	fuse_opt_add_arg(args, "-ovolname=DDD");
	
	// FUSE contains a feature where it automatically implements 'soft' delete if one process has a file open when another calls unlink().
	// This feature causes us a bunch of problems, so we use "-ohard_remove" to disable it, and track the needed 'soft delete' functionality on our own.
	fuse_opt_add_arg(args, "-ohard_remove");
	fuse_opt_add_arg(args, "-obig_writes");
	fuse_opt_add_arg(args, "-ofsname=blobfs");
	fuse_opt_add_arg(args, "-okernel_cache");
}


void *azs_init(struct fuse_conn_info * conn)
{
	openlog("", 0, 0);
	// Retrieve storage account from connection string.
	AZS_DEBUGLOGV("start");
	utility::string_t storage_connection_string(U("DefaultEndpointsProtocol=http"));
	storage_connection_string += U(";AccountName=");
	storage_connection_string += string2wstring(str_options.accountName);
	storage_connection_string += U(";AccountKey=");
	storage_connection_string += string2wstring(str_options.accountKey);

	azure::storage::cloud_storage_account storage_account = azure::storage::cloud_storage_account::parse(storage_connection_string);

	// Create the blob client.
	azure::storage::cloud_blob_client blob_client = storage_account.create_cloud_blob_client();

	// Retrieve a reference to a previously created container.
	azure_blob_container = std::make_shared<azure::storage::cloud_blob_container>(blob_client.get_container_reference(string2wstring(str_options.containerName)));
	
	Uploader::run();
	
	azure_blob_container->download_attributes();
	auto& containermeta = azure_blob_container->metadata();
	auto iter = containermeta.find(L"rootdir");
	auto iter2 = containermeta.find(L"blocksize");
	if (iter == containermeta.end()|| iter2 == containermeta.end() || std::stoll(iter2->second) != default_block_size) {
		guid_t uid = utility::new_uuid();
		Directory* dir = FileMap::instance()->create_dir(uid);
		dir->addEntry(_XPLATSTR("."), uid);
		dir->addEntry(_XPLATSTR(".."), uid);
		containermeta[L"rootdir"] = utility::uuid_to_string(uid);
		containermeta[L"blocksize"] = my_to_string(default_block_size);
		azure_blob_container->upload_metadata();
		rootdir = uid;
	}
	else {
		rootdir = utility::string_to_uuid(iter->second);
	}

	/*
	cfg->attr_timeout = 360;
	cfg->kernel_cache = 1;
	cfg->entry_timeout = 120;
	cfg->negative_timeout = 120;
	*/
	conn->max_write = 4194304;
	//conn->max_read = 4194304;
	conn->max_readahead = 4194304;
	// conn->max_background = 128;
	//  conn->want |= FUSE_CAP_WRITEBACK_CACHE | FUSE_CAP_EXPORT_SUPPORT; // TODO: Investigate putting this back in when we downgrade to fuse 2.9

	//gc_cache.run();
	AZS_DEBUGLOGV("ended");
	return NULL;
}



int azs_statfs(const char *path, struct statvfs * stbuf)
{
	AZS_DEBUGLOGV("path = %s", path);
	std::string pathString(path);

	struct FUSE_STAT statbuf;
	stbuf->f_bsize = l_blob_adapter::default_block_size/512;
	stbuf->f_blocks = 1000000000000;
	stbuf->f_bfree = 100000000000;
	stbuf->f_bavail = 100000000000;
	int getattrret = azs_getattr(path, &statbuf);
	if (getattrret != 0)
	{
		AZS_DEBUGLOGV("end error : %d, path = %s",getattrret, path);
		return getattrret;
	}

	//// return tmp path stats
	//errno = 0;
	////int res = statvfs(str_options.tmpPath.c_str(), stbuf);
	////if (res == -1)
	////	return -errno;
	AZS_DEBUGLOGV("end");
	return 0;
}


int azs_getattr(const char *path, struct FUSE_STAT * stbuf)
{
	// AZS_DEBUGLOGV("path = %s", path);
	CommonFile * t = parse_path(path);
	if (t == nullptr) {
		errno = ENOENT;
		AZS_DEBUGLOGV("end error : %d , path : %s", errno, path);
		return -ENOENT;
	}
	else {
		int r = t->azs_getattr(stbuf, default_permission);
		if (r)errno = r;
		//AZS_DEBUGLOGV("end");
		return -r;
	}
}


int azs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, FUSE_OFF_T, struct fuse_file_info *)
{
	AZS_DEBUGLOGV("path = %s", path);
	Directory* t = parse_path(path)->to_dir();
	if (t == nullptr) {
		errno = ENOENT;
		AZS_DEBUGLOGV("end error : %d, path = %s", errno, path);
		return -1;
	}
	else {
		int r = t->azs_readdir(buf, filler);
		AZS_DEBUGLOGV("end");
		return r;
	}
}


static int azs_open(const char *path, struct fuse_file_info *fi)
{
	AZS_DEBUGLOGV("path = %s", path);

	CommonFile * file = parse_path(path);
	if (file == nullptr) {
		errno = ENOENT;
		AZS_DEBUGLOGV("end error : %d, path = %s", errno, path);
		return -1;
	}
	if (file->get_type()==FileType::F_Directory) {
		errno = EISDIR;
		AZS_DEBUGLOGV("end error : %d, path = %s", errno, path);
		return -1;
	}
	fhwraper* fh = new fhwraper(file);
	fh->flag = fi->flags & 0x3;
	fi->fh = (uint64_t)fh;
	AZS_DEBUGLOGV("end, handle : %llx", fi->fh);
	return 0;
}

static int azs_read(const char * path, char * buf, size_t size, FUSE_OFF_T offset, struct fuse_file_info * fi)
{
	AZS_DEBUGLOGV("path = %s, offset : %lld, size : %lld", path,offset,size);
	fhwraper *fh = (fhwraper *)(fi->fh);
	CommonFile *file = fh->file;
	auto f = file->to_reg();
	if (fh->flag & 0x1 || !f) {
		errno = EBADF;
		return -1;
	}
	int readcnt= f->read(offset, size, (uint8_t*)buf);
	AZS_DEBUGLOGV("end, read : %d, handle : %llx", readcnt, fi->fh);
	return readcnt;
}

static int azs_write(const char *path, const char *buf, size_t size, FUSE_OFF_T offset, struct fuse_file_info *fi) {
	AZS_DEBUGLOGV("path = %s, offset : %lld, size : %lld", path, offset, size);
	fhwraper *fh = (fhwraper *)(fi->fh);
	CommonFile *file = fh->file;
	auto f = file->to_reg();
	if (!(fh->flag & 0x3)||!f) {
		errno = EBADF;
		return -1;
	}
	int writecnt = f->write(offset, size, (uint8_t*)buf);
	AZS_DEBUGLOGV("end, write : %d, handle : %llx", writecnt, fi->fh);
	return writecnt;
}



int azs_truncate(const char * path, FUSE_OFF_T offset) {
	AZS_DEBUGLOGV("path = %s, size : %lld", path, offset);
	CommonFile * file = parse_path(path);
	if (file == nullptr) {
		errno = ENOENT;
		AZS_DEBUGLOGV("end error : %d, path = %s", errno, path);
		return -1;
	}
	if (file->get_type() == FileType::F_Directory) {
		errno = EISDIR;
		AZS_DEBUGLOGV("end error : %d, path = %s", errno, path);
		return -1;
	}
	RegularFile * reg = file->to_reg();
	reg->trancate(offset);
	AZS_DEBUGLOGV("end, trancate : %lld", offset);
	return 0;
}

int azs_ftruncate(const char * path, FUSE_OFF_T offset, struct fuse_file_info *fi) {
	AZS_DEBUGLOGV("path = %s, size : %lld", path, offset);
	fhwraper *fh = (fhwraper *)(fi->fh);
	CommonFile *file = fh->file;
	auto f = file->to_reg();
	if (!(fh->flag & 0x3) || !f) {
		errno = EBADF;
		AZS_DEBUGLOGV("end error : %d, path = %s", errno, path);
		return -1;
	}
	f->trancate(offset);
	AZS_DEBUGLOGV("end, trancate : %lld", offset);
	return 0;
}

int azs_create(const char *path, mode_t mode, struct fuse_file_info *fi) {
	AZS_DEBUGLOGV("path = %s, mode : %u", path, mode);
	
	string_t parentname;
	string_t shortname;
	split_path(path, parentname, shortname);
	if (shortname.empty()) {
		syslog(LOG_NOTICE, "short name invalid, path= %s\n", path);
		errno = EACCES;
		return -1;
	}
	Directory* parent = parse_path(parentname)->to_dir();
	if (parent == nullptr) {
		errno = EACCES;
		AZS_DEBUGLOGV("end error : %d, path = %s", errno, path);
		return -1;
	}
	guid_t uid= parent->create_reg(shortname);
	if (uid == guid_t()) {
		AZS_DEBUGLOGV("error , path = %s", path);
		return -1;
	}
	CommonFile* file = FileMap::instance()->get(uid);
	fhwraper* fh = new fhwraper(file);
	fh->flag = fi->flags & 0x3;
	fi->fh = (uint64_t)fh;
	AZS_DEBUGLOGV("end, handle : %llx", fi->fh);
	return 0;
}

int azs_release(const char *path, struct fuse_file_info * fi) {
	AZS_DEBUGLOGV("path = %s, handle : %llx", path,fi->fh);
	delete (fhwraper*)(fi->fh);
	fi->fh = 0;
	AZS_DEBUGLOGV("end");
	return 0;
}

int azs_fsync_stub(const char * /*path*/, int /*isdatasync*/, struct fuse_file_info * /*fi*/) {
	return 0;
}

int azs_mkdir(const char *path, mode_t) {
	AZS_DEBUGLOGV("path = %s", path);
	
	string_t parentname ;
	string_t shortname ;
	split_path(path, parentname, shortname);
	if (shortname.empty()) {
		syslog(LOG_NOTICE, "short name invalid, path= %s\n", path);
		errno = EACCES;
		AZS_DEBUGLOGV("end error : %d, path = %s", errno, path);
		return -1;
	}
	Directory* parent = parse_path(parentname)->to_dir();
	if (parent == nullptr) {
		errno = EACCES;
		AZS_DEBUGLOGV("end error : %d, path = %s", errno, path);
		return -1;
	}
	guid_t uid = parent->create_dir(shortname);
	if (uid == guid_t()) {
		AZS_DEBUGLOGV("error , path = %s", path);
		return -1;
	}
	AZS_DEBUGLOGV("end");
	return 0;
}

int azs_unlink(const char *path) {
	AZS_DEBUGLOGV("path = %s", path);
	
	string_t parentname;
	string_t shortname;
	split_path(path, parentname, shortname);
	Directory* parent = parse_path(parentname)->to_dir();
	/*RegularFile * file = parse_path(path)->to_reg();
	if (file == nullptr) {
		errno = ENOENT;
		return -1;
	}*/
	if (parent == nullptr) {
		errno = EACCES;
		AZS_DEBUGLOGV("end error : %d, path = %s", errno, path);
		return -1;
	}
	bool sucess = parent->rmEntry(shortname);
	if (!sucess) {
		errno = ENOENT;
		AZS_DEBUGLOGV("end error : %d, path = %s", errno, path);
		return -1;
	}
	AZS_DEBUGLOGV("end");
	return 0;
}

int azs_rmdir(const char *path) {
	AZS_DEBUGLOGV("path = %s", path);
	string_t parentname;
	string_t shortname;
	split_path(path, parentname, shortname);
	Directory* parent = parse_path(parentname)->to_dir();
	Directory * file = parse_path(path)->to_dir();
	if (parent == nullptr) {
		errno = EACCES;
		AZS_DEBUGLOGV("end error : %d, path = %s", errno, path);
		return -1;
	}
	if (file == nullptr) {
		errno = ENOENT;
		AZS_DEBUGLOGV("end error : %d, path = %s", errno, path);
		return -1;
	}
	if (file->entry_cnt() > 2) {
		errno = ENOTEMPTY;
		AZS_DEBUGLOGV("end error : %d, path = %s", errno, path);
		return -1;
	}
	file->rmEntry(_XPLATSTR(".."));
	file->rmEntry(_XPLATSTR("."));

	bool sucess = parent->rmEntry(shortname);
	if (!sucess) {
		errno = ENOENT;
		AZS_DEBUGLOGV("end error : %d, path = %s", errno, path);
		return -1;
	}
	AZS_DEBUGLOGV("end");
	return 0;
}

int azs_chown_stub(const char * /*path*/, uid_t /*uid*/, gid_t /*gid*/) {
	return 0;
}

int azs_chmod_stub(const char * /*path*/, mode_t /*mode*/)
{
	//TODO: Implement
	//    return -ENOSYS;
	return 0;
}

int azs_utimens(const char * /*path*/, const struct timespec[2] /*ts[2]*/)
{
	//TODO: Implement
	//    return -ENOSYS;
	return 0;
}

void azs_destroy(void * /*private_data*/)
{
	AZS_DEBUGLOGV("azs_destroy called\n");
	Uploader::stop();
}

int azs_access(const char *path, int mask) {
	AZS_DEBUGLOGV("path = %s : mask= %d", path, mask);
	CommonFile* file = parse_path(path);
	if (!file || file->exist()) {
		errno = ENOENT;
		AZS_DEBUGLOGV("end error : %d, path = %s", errno, path);
		return -1;
	}
	AZS_DEBUGLOGV("end");
	return 0;
}

int azs_rename(const char *src, const char *dst) {
	AZS_DEBUGLOGV("src = %s : dst= %s", src,dst);
	
	string_t parentname;
	string_t shortname;
	split_path(src, parentname, shortname);
	Directory* parent = parse_path(parentname)->to_dir();
	
	
	string_t dparentname;
	string_t dshortname;
	split_path(dst, dparentname, dshortname);
	Directory* dparent = parse_path(dparentname)->to_dir();

	if (dshortname.empty()) {
		syslog(LOG_NOTICE, "short name invalid, path= %s\n", dst);
		errno = EACCES;
		AZS_DEBUGLOGV("end error : %d, src = %s ,dst = %s", errno, src,dst);
		return -1;
	}

	if (!(parent&&dparent)) {
		errno = EACCES;
		AZS_DEBUGLOGV("end error : %d, src = %s ,dst = %s", errno, src, dst);
		return -1;
	}

	guid_t uid = parent->find( shortname );
	guid_t otheruid = dparent->find(dshortname);

	if (uid == guid_t()) {
		errno = ENOENT;
		AZS_DEBUGLOGV("end error : %d, src = %s ,dst = %s", errno, src, dst);
		return -1;
	}
	if (otheruid != guid_t()) {
		errno = EEXIST;
		AZS_DEBUGLOGV("end error : %d, src = %s ,dst = %s", errno, src, dst);
		return -1;
	}
	
	dparent->addEntry(dshortname, uid);
	parent->rmEntry(shortname);
	AZS_DEBUGLOGV("end");
	return 0;
}

int azs_setxattr(const char * /*path*/, const char * /*name*/, const char * /*value*/, size_t /*size*/, int /*flags*/)
{
	return -ENOSYS;
}
int azs_getxattr(const char * /*path*/, const char * /*name*/, char * /*value*/, size_t /*size*/)
{
	return -ENOSYS;
}
int azs_listxattr(const char * /*path*/, char * /*list*/, size_t /*size*/)
{
	return -ENOSYS;
}
int azs_removexattr(const char * /*path*/, const char * /*name*/)
{
	return -ENOSYS;
}

int azs_flush(const char *path, struct fuse_file_info *fi) {
	return 0;
}

void set_up_callbacks() {
	azs_fuse_operations.init = azs_init;
	azs_fuse_operations.statfs = azs_statfs;
	azs_fuse_operations.access = azs_access;
	azs_fuse_operations.getattr = azs_getattr;
	azs_fuse_operations.readdir = azs_readdir;
	azs_fuse_operations.read = azs_read;
	azs_fuse_operations.write = azs_write;
	azs_fuse_operations.open = azs_open;
	azs_fuse_operations.create = azs_create;
	azs_fuse_operations.release = azs_release;
	azs_fuse_operations.fsync = azs_fsync_stub;
	azs_fuse_operations.mkdir = azs_mkdir;
	azs_fuse_operations.unlink = azs_unlink;
	azs_fuse_operations.rmdir = azs_rmdir;
	azs_fuse_operations.chown = azs_chown_stub;
	azs_fuse_operations.chmod = azs_chmod_stub;
	azs_fuse_operations.utimens = azs_utimens;
	azs_fuse_operations.destroy = azs_destroy;
	azs_fuse_operations.truncate = azs_truncate;
	azs_fuse_operations.ftruncate = azs_ftruncate;
	azs_fuse_operations.rename = azs_rename;
	azs_fuse_operations.setxattr = azs_setxattr;
	azs_fuse_operations.getxattr = azs_getxattr;
	azs_fuse_operations.listxattr = azs_listxattr;
	azs_fuse_operations.removexattr = azs_removexattr;
	azs_fuse_operations.flush = azs_flush;
}