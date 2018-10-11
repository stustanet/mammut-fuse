#include "module.h"

#include <unistd.h>
#include <dirent.h>
#include <sys/types.h>
#include <string.h>
#include <syslog.h>

#include <iostream>
#include <sstream>

#include "config.h"
#include "communicator.h"

namespace mammutfs {

static Module::LOG_LEVEL str_to_loglevel(const std::string &str) {
	if ("TRACE" == str) {
		return Module::LOG_LEVEL::TRACE;
	} else if ("INFO" == str) {
		return Module::LOG_LEVEL::INFO;
	} else if ("WARN" == str) {
		return Module::LOG_LEVEL::WRN;
	} else if ("ERROR" == str) {
		return Module::LOG_LEVEL::ERR;
	} else {
		// TODO WARN
		std::cerr << "Could not convert loglevel, will use TRACE" << std::endl;
		return Module::LOG_LEVEL::TRACE;
	}
}


Module::Module(const std::string &modname,
               const std::shared_ptr<MammutConfig> &config,
               const std::shared_ptr<Communicator> &comm) :
	config(config),
	comm(comm),
	modname(modname),
	max_native_fds(0) {
	this->config->lookupValue("max_native_fds", this->max_native_fds);
	{
		std::string tmp;
		this->config->lookupValue("loglevel", tmp);
		this->max_loglvl = str_to_loglevel(tmp);
	}
#ifdef SAVE_FILE_HANDLES
	config->register_changeable("max_native_fds", [this]() {
			this->config->lookupValue("max_native_fds", this->max_native_fds);
			// TODO maybe close enough files to reach the limit
		});
#endif

	config->register_changeable("loglevel", [this]() {
			std::string tmp;
			this->config->lookupValue("loglevel", tmp);
			this->max_loglvl = str_to_loglevel(tmp);
		});

	this->comm->register_command(
		modname + "_raid",
		[this](const std::string &/*data*/, std::string &resp) {
			if (this->basepath == "") {
				std::string path;
				find_raid(path);
			}
			std::stringstream ss;
			ss << "\"" << this->basepath << "\"";
			resp = ss.str();
			return true;
		}, "Get the modules identified raid");
}


int Module::translatepath(const std::string &path, std::string &out) {
	std::string basepath;
	int retval = find_raid(basepath);
	out = basepath + path;
	return retval;
}


int Module::find_raid(std::string &path) {
	if (basepath != "") {
		path = basepath;
		return 0;
	}

	for (const auto &raid : config->raids) {
		std::string to_test = raid + "/" + modname + "/" + config->username();

		//this->log(LOG_LEVEL::INFO, std::string("Testing raid " + to_test));
		struct stat statbuf;
		memset(&statbuf, 0, sizeof(statbuf));
		int retval = ::stat(to_test.c_str(), &statbuf);

		if (retval == 0) {
			//this->log(LOG_LEVEL::INFO, std::string("Found raid at " + to_test));
			basepath = to_test;
			break;
		}
	}
	if (basepath == "") {
		this->log(LOG_LEVEL::ERR, "Could not find Raid!! THIS IS BAD!");
		return -ENOENT;
	}

	path = basepath;
	return 0;
}

void Module::log(LOG_LEVEL lvl, const std::string &msg, const std::string &path) {
	if (static_cast<int>(lvl) < static_cast<int>(this->max_loglvl)) {
		return;
	}

	std::string prefix, suffix = "\033[0m";
	switch(lvl) {
#ifdef ENABLE_TRACELOG
	case LOG_LEVEL::TRACE:
		prefix = "\033[0m"; // nothing;
		break;
#endif
	case LOG_LEVEL::INFO:
		prefix = "\033[36m"; // blue;
		break;
	case LOG_LEVEL::WRN:
		prefix = "\033[33m"; // yellow
		break;
	case LOG_LEVEL::ERR:
		prefix = "\033[31m"; // red
		break;
	default:
		break;
	}

	std::stringstream ss;
	ss <<  "[" << modname << "] " <<  msg;
	if (path != "") {
		ss << ": " << path;
	}
	std::cerr << prefix << ss.str() << suffix << std::endl;

	// WRN and ERR to syslog
	switch(lvl) {
	default:
		break;
	case LOG_LEVEL::INFO:
		syslog(LOG_INFO, ss.str().c_str());
		break;
	case LOG_LEVEL::WRN:
		syslog(LOG_WARNING, ss.str().c_str());
		break;
	case LOG_LEVEL::ERR:
		syslog(LOG_ERR, ss.str().c_str());
		break;
	}
}


#ifdef ENABLE_TRACELOG
void Module::trace(const std::string &method,
                   const std::string &path,
                   const std::string &second_path) {
	std::stringstream ss;
	ss << method << ": " << path;
	if (second_path != "") {
		ss << " --> " << second_path;
	}
	log(LOG_LEVEL::TRACE, ss.str());
}
#endif


void Module::info(const std::string &method,
                  const std::string &message,
                  const std::string &path,
                  const std::string &second_path) {

	std::stringstream ss;
	ss << "INFO: " << method << ": " << message << ": " << path;
	if (second_path != "") {
		ss << " --> " << second_path;
	}
	log(LOG_LEVEL::WRN, ss.str());
}


void Module::warn(const std::string &method,
                  const std::string &message,
                  const std::string &path,
                  const std::string &second_path) {

	std::stringstream ss;
	ss << "WARN: " << method << ": " << message << ": " << path;
	if (second_path != "") {
		ss << " --> " << second_path;
	}
	log(LOG_LEVEL::WRN, ss.str());
}


void Module::error(const std::string &method,
                   const std::string &path,
                   const std::string &second_path) {
	std::stringstream ss;
	ss << "ERROR: " << method << ": " << errno << " " << strerror(errno) << ": " << path;
	if (second_path != "") {
		ss << " --> " << second_path;
	}
	log(LOG_LEVEL::ERR, ss.str());
}

// A default file handle does nothing
Module::open_file_handle_t::open_file_handle_t(open_file_t *f, bool close) :
	file(f),
	should_close(close) {
}

Module::open_file_handle_t::open_file_handle_t(open_file_handle_t &&rhs) :
	file(rhs.file),
	should_close(rhs.should_close) {
	rhs.file = nullptr;
}

// Closes the file upon leaving the control structure, releasing its contents
// and especially its precious file descriptors.
Module::open_file_handle_t::~open_file_handle_t() {
#ifdef SAVE_FILE_HANDLES
	if (this->file != nullptr) {
		if (this->should_close && this->file->is_open) {
			switch(this->file->type) {
			case open_file_t::FILE:
				::close(this->file->fh.fd);
				break;
			case open_file_t::DIRECTORY:
				::closedir(this->file->fh.dp);
				break;
			default:
				break;
			}
			this->file->is_open = false;
		}
	}
#endif
}

int Module::open_file_handle_t::fd() {
	if (this->file->type != open_file_t::FILE) {
		errno = EINVAL;
		return -1;
	}

#ifdef SAVE_FILE_HANDLES
	if (!this->file->is_open) {
		// Remember R/W status, and set the remaining stuff correctly
		int flags = (this->file->flags & (O_RDONLY | O_WRONLY | O_RDWR)) | O_NOFOLLOW | O_APPEND;
		this->file->fh.fd = ::open(this->file->path.c_str(), flags);
		if (this->file->fh.fd < 0) {
			std::cout << "ERROR opening file: " << strerror(errno) << std::endl;
		}
		this->file->is_open = true;
	}
#endif
	return this->file->fh.fd;
}

DIR *Module::open_file_handle_t::dp() {
	if (this->file->type != open_file_t::DIRECTORY) {
		errno = EINVAL;
		return NULL;
	}
#ifdef SAVE_FILE_HANDLES
	if (!this->file->is_open) {
		this->file->fh.dp = ::opendir(this->file->path.c_str());
		this->file->is_open = true;
	}
#endif
	return this->file->fh.dp;
}

Module::open_file_handle_t Module::file(const std::string &path, fuse_file_info *fi) {
	// Check if there is already an open file.
	// If not, create a open file structure, but do not assign any sane values
	// to it, it is not possible to do that cleanly at this point.
	// Finally, create a open_file_handle_t for this file.
	// Use the number of open files as reference for the should_close flag;
	const auto &lock = std::lock_guard<std::mutex>(this->open_file_mux);

	open_file_t *file;
	auto it = open_files.find(fi->fh);
	if (it != open_files.end()) {
		file = &it->second;
	} else {
		open_file_t f;
		f.path = path; // TODO do we have to store the path?
		f.is_open = false;
		f.has_changed = false;
		f.type = open_file_t::UNSPEC;
		f.flags = 0;
		f.fh.fd = -1;

		int64_t fileid = this->open_file_count++;
		fi->fh = fileid;

		file = &open_files.emplace(std::make_pair(fileid, f)).first->second;
	}

	bool should_close = open_files.size() > max_native_fds;

	return open_file_handle_t(file, should_close);
}

void Module::close_file(const std::string &/*path*/, fuse_file_info *fi) {
	// Test if the file was open, if so - remove it and thereby close it.
	const auto &lock = std::lock_guard<std::mutex>(this->open_file_mux);

	auto it = open_files.find(fi->fh);
	if (it != open_files.end()) {
		// We do not need to call close, will be done in destructor
		open_files.erase(it);
	}
}

void Module::close_file(const char *path, fuse_file_info *fi) {
	close_file(std::string(path), fi);
}


void Module::dump_open_files(std::ostream &s) {
	const auto& lock = std::lock_guard<std::mutex>(this->open_file_mux);
	for (const auto &t : open_files) {
		s << "{\"key\":\"" << t.first << "\","
		  << "\"name\":\"" << t.second.path << "\","
		  << "\"changed\":\"" << t.second.has_changed << "\","
		  << "\"open\":\"" << t.second.is_open << "\"},";
	}
}

int Module::getattr(const char *path, struct stat *statbuf) {
	// Since getattr is spaming a _lot_, it should be more quiet to make the
	// remaining output more readable.
#ifdef TRACE_GETATTR
	this->trace("getattr", path);
#endif
	memset(statbuf, 0, sizeof(*statbuf));
	if (strcmp(path, "/") == 0) {
		statbuf->st_dev         = 0;               // IGNORED Device
		statbuf->st_ino         = 1;
		statbuf->st_mode        = S_IFDIR | 0755;  // Protection
		statbuf->st_nlink       = 1;               // Number of Hard links
		statbuf->st_uid         = config->user_uid;
		statbuf->st_gid         = config->user_gid;
		statbuf->st_rdev        = 0;
		statbuf->st_size        = 0;
		statbuf->st_blocks      = 0;
		statbuf->st_atim.tv_sec = 0;  // Last Access
		statbuf->st_mtim.tv_sec = 0;  // Last Modification
		statbuf->st_ctim.tv_sec = 0;  // Last Status change
		return 0;
	}

	int retstat = 0;
	std::string translated;
	if ((retstat = this->translatepath(path, translated)) != 0) {
		this->info("getattr", "translatepath failed", path);
	} else {
		retstat = ::lstat(translated.c_str(), statbuf);
		if (retstat) {
			retstat = -errno;
			if (errno != ENOENT) {
				this->warn("getattr", "lstat failed", translated);
			}
		}
	}

	// Eliminate all User-IDs from the items
	//statbuf->st_uid = config->anon_uid;
	//statbuf->st_gid = config->anon_gid;
	return retstat;
}


int Module::readlink(const char *path, char */*link*/, size_t /*size*/) {
	this->trace("readlink", path);
	return -ENOTSUP;
	// It can be dangerous reading arbitrary symlinks. We need to do a lot of
	// sanitizing to allow such things. Only relative symlinks within the
	// module should be supported - if they are supported at all.
	// Symlinks are the most serious thread to mammut, everything else is
	// pretty easy to contain.
	/*this->trace("readlink", path);
	int retstat = 0;

	if (size == 0)
		return -EINVAL;

	std::string translated;
	retstat = this->translatepath(path, translated);

	retstat = ::readlink(translated.c_str(), link, size - 1);
	if (retstat < 0) {
		retstat = -errno;
		this->warn("readlink", "readlink failed", path);
	} else {
		link[retstat] = '\0';
		retstat       = 0;
	}

	return retstat;*/
}

/* Deprecated, use readdir() instead */
int Module::getdir(const char *path, fuse_dirh_t, fuse_dirfil_t) {
	this->trace("getdir", path);
	return -ENOTSUP;
}


int Module::mknod(const char *path, mode_t, dev_t) {
	this->trace("mknod", path);
	return -ENOTSUP;
}


int Module::mkdir(const char *path, mode_t mode) {
	this->trace("mkdir", path);

	int retstat = 0;
	std::string translated;
	if ((retstat = this->translatepath(path, translated))) {
		this->info("mkdir", "translatepath failed", path);
		return retstat;
	}

	if ((retstat = ::mkdir(translated.c_str(), mode)) < 0) {
		retstat = -errno;
		this->warn("mkdir", "mkdir failed", translated);
	}

	return retstat;
}


int Module::unlink(const char *path) {
	this->trace("unlink", path);

	int retstat = 0;
	std::string translated;
	if ((retstat = this->translatepath(path, translated))) {
		this->info("unlink", "translatepath failed", path);
		return retstat;
	}

	if ((retstat = ::unlink(translated.c_str()))) {
		retstat = -errno;
		this->warn("unlink", "unlink", translated);
	}

	return retstat;
}


int Module::rmdir(const char *path) {
	this->trace("rmdir", path);

	int retstat = 0;
	std::string translated;
	if ((retstat = this->translatepath(path, translated))) {
		this->info("rmdir", "translatepath failed", path);
		return retstat;
	}

	if ((retstat = ::rmdir(translated.c_str()))) {
		retstat = -errno;
		this->warn("rmdir", "rmdir", translated);
	}

	return retstat;
}


int Module::symlink(const char *path, const char *) {
	this->trace("symlink", path);
	return -ENOTSUP;
}


int Module::rename(const char *sourcepath,
                   const char *newpath,
                   const char */*sourcepath_raw*/,
                   const char */*newpath_raw*/) {
	this->trace("rename", sourcepath, newpath);

	int retstat = 0;
	std::string to_translated;
	if ((retstat = this->translatepath(newpath, to_translated))) {
		this->info("rename", "translatepath failed", newpath);
		return retstat;
	}

	if ((retstat = ::rename(sourcepath, to_translated.c_str()) < 0)) {
		retstat = -errno;
		this->warn("rename", "rename", sourcepath, to_translated);
	} else {
		// Open files do not need to be modified, because of filesystem magic
		// that linux provides - a file is not re-identified by its name but
		// by its filedescriptor (like it has to be)
	}

	return retstat;
}


int Module::chmod(const char *path, mode_t mode) {
	this->trace("chmod", path);

	int retstat = 0;
	std::string translated;
	if ((retstat = this->translatepath(path, translated))) {
		this->info("chmod", "translatepath failed", path);
		return retstat;
	}

	if ((retstat = ::chmod(translated.c_str(), mode)) < 0) {;
		retstat = -errno;
		this->warn("chmod", "chmod", translated);
	}

	return retstat;
}


int Module::chown(const char *path, uid_t, gid_t) {
	this->trace("chown", path);
	return -EPERM;
}


int Module::truncate(const char *path, off_t newsize) {
	this->trace("truncate", path);

	int retstat = 0;
	std::string translated;
	if ((retstat = this->translatepath(path, translated))) {
		this->info("truncate", "translatepath failed", path);
		return retstat;
	}

	if (newsize > config->truncate_max_size()) {
		struct stat st;
		memset(&st, 0, sizeof(st));
		if (::stat(translated.c_str(), &st) != 0) {
			this->warn("truncate", "stat", translated);
			return -errno;
		}

		if (st.st_size < newsize) {
			return -EPERM;
		}
	}

	retstat = ::truncate(translated.c_str(), newsize);
	if (retstat < 0) {
		retstat = -errno;
		this->warn("truncate", "truncate", translated);
	} else {
		// We cannot link to an open file.
	}

	return retstat;
}

int Module::open(const char *path, struct fuse_file_info *fi) {
	this->trace("open", path);

	int retstat = 0;
	std::string translated;
	if ((retstat = this->translatepath(path, translated))) {
		this->info("open", "translatepath failed", path);
		return retstat;
	}

	// How not to follow symlinks
	fi->flags |= O_NOFOLLOW;
	int fd = ::open(translated.c_str(), fi->flags);
	if (fd < 0) {
		retstat = -errno;
		this->warn("open", "open", translated);
	} else {
		auto f = this->file(translated, fi);
		f.file->type = open_file_t::FILE;
		f.file->fh.fd = fd;
		f.file->is_open = true;
		f.file->has_changed = false;
		f.file->flags = fi->flags;
	}

	return retstat;
}


int Module::read(const char *path, char *buf, size_t size, off_t offset,
                 struct fuse_file_info *fi) {
	this->trace("read", path);

	int retstat = 0;
	std::string translated;
	if ((retstat = this->translatepath(path, translated))) {
		this->info("read", "translatepath failed", path);
		return retstat;
	}
	auto f = this->file(translated, fi);

	retstat = ::pread(f.fd(), buf, size, offset);
	if (retstat < 0) {
		this->warn("read", "pread", translated);
		retstat = -errno;
	}

	return retstat;
}


int Module::write(const char *path, const char *buf, size_t size, off_t offset,
                  struct fuse_file_info *fi) {
	this->trace("write", path);

	//dump_open_files(std::cout << "open files: ");
	//std::cout << std::endl;

	int retstat = 0;
	std::string translated;
	if ((retstat = this->translatepath(path, translated))) {
		this->info("write", "translatepath failed", path);
		return retstat;
	}
	auto f = this->file(translated, fi);
	int fd = f.fd();
	retstat = ::pwrite(fd, buf, size, offset);
	if (retstat < 0) {
		retstat = -errno;
		this->warn("write", "pwrite", translated);
	} else {
		f.file->has_changed = true;
	}

	return retstat;
}


int Module::statfs(const char *path, struct statvfs *statv) {
	this->trace("statfs", path);

	int retstat = 0;
	std::string translated;
	if ((retstat = this->translatepath(path, translated))) {
		this->info("statfs", "translatepath failed", path);
		return retstat;
	}

	// get stats for underlying filesystem
	memset(statv, 0, sizeof(*statv));
	retstat = ::statvfs(translated.c_str(), statv);
	if (retstat < 0) {
		retstat = -errno;
		this->warn("statfs", "statvfs", translated);
	}

	return retstat;
}


int Module::flush(const char *path, struct fuse_file_info *) {
	this->trace("flush", path);
	return 0;
}


int Module::release(const char *path, struct fuse_file_info *fi) {
	this->trace("release", path);

	int retstat = 0;
	std::string translated;
	if ((retstat = this->translatepath(path, translated))) {
		this->info("stat", "translatepath failed", path);
		return retstat;
	}

	auto f = this->file(translated, fi);
	if (f.file->is_open) {
		retstat = close(f.fd());
		f.file->is_open = false;
		f.file->fh.fd = -1;
	}

	this->close_file(path, fi);

	return retstat;
}


int Module::fsync(const char *path, int, struct fuse_file_info *fi) {
	this->trace("fsync", path);

	int retstat = 0;
	// This is only useful if we were not closing the file all the time
#ifndef SAVE_FILE_HANDLES
	std::string translated;
	if ((retstat = this->translatepath(path, translated))) {
		this->info("fsync", "translatepath failed", path);
		return retstat;
	}

	auto f = this->file(translated, fi);
	retstat = ::fsync(f.fd());
	if (retstat < 0) {
		errno = -retstat;
		this->warn("fsync", "fsync", translated);
	}
#else
	(void)fi;
#endif
	return retstat;
}


int Module::setxattr(const char *path, const char *, const char *, size_t, int) {
	this->trace("setxattr", path);
	return -ENOTSUP;
}

/** Get extended attributes */
int Module::getxattr(const char *path, const char *, char *, size_t) {
	this->trace("getxattr", path);
	return -ENOTSUP;
}

/** List extended attributes */
int Module::listxattr(const char *path, char *, size_t) {
	this->trace("listxattr", path);
	return -ENOTSUP;
}

/** Remove extended attributes */
int Module::removexattr(const char *path, const char *) {
	this->trace("removexattr", path);
	return -ENOTSUP;
}


int Module::opendir(const char *path, struct fuse_file_info *fi) {
	this->trace("opendir", path);

	int retstat = 0;
	std::string translated;
	if ((retstat = this->translatepath(path, translated))) {
		this->info("opendir", "translatepath failed", path);
		return retstat;
	}

	DIR *dp = ::opendir(translated.c_str());
	if (dp == NULL) {
		retstat = -errno;
		this->warn("opendir", "opendir", translated);
		return retstat;
	} else {
		auto f = this->file(translated, fi);
		f.file->type = open_file_t::DIRECTORY;
		f.file->fh.dp = dp;
		f.file->is_open = true;
		f.file->has_changed = false;
	}

	return 0;
}


int Module::readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                    off_t offset, struct fuse_file_info *fi) {
	this->trace("readdir", path);
	(void)offset;

	int retstat = 0;
	std::string translated;
	if ((retstat = this->translatepath(path, translated))) {
		this->info("readdir", "translatepath failed", path);
		return retstat;
	}
	auto f = this->file(translated, fi);
	DIR *dp = f.dp();

	if (dp == 0) {
		return -EINVAL;
	}

	rewinddir(dp);

	struct dirent *de;
	while ((de = ::readdir(dp)) != NULL) {
		if (filler(buf, de->d_name, NULL, 0) != 0) {
			return -ENOMEM;
		}
	}

	return 0;
}


int Module::releasedir(const char *path, struct fuse_file_info *fi) {
	this->trace("releasedir", path);

	int retstat = 0;
	std::string translated;
	if ((retstat = this->translatepath(path, translated))) {
		this->info("releasedir", "translatepath failed", path);
		return retstat;
	}
	auto f = this->file(translated, fi);
	if (f.file->is_open && f.file->type == open_file_t::DIRECTORY) {
		retstat = ::closedir(f.dp());
		f.file->is_open = false;
	}

	this->close_file(path, fi);

	return retstat;
}


int Module::fsyncdir(const char *path, int, struct fuse_file_info *) {
	this->trace("fsyncdir", path);

	return 0;
}


int Module::access(const char *path, int mask) {
	this->trace("access", path);
	int retstat = 0;
	std::string translated;
	if ((retstat = this->translatepath(path, translated))) {
		this->info("access", "translatepath failed", path);
		return retstat;
	}
	retstat = ::access(translated.c_str(), mask);
	return retstat;
}


int Module::create(const char *path, mode_t mode, struct fuse_file_info *fi) {
	this->trace("create", path);

	int retstat = 0;
	std::string translated;
	if ((retstat = this->translatepath(path, translated))) {
		this->info("create", "translatepath failed", path);
		return retstat;
	}

	int fd = ::creat(translated.c_str(), mode);
	if (fd < 0) {
		retstat = -errno;
		this->warn("create", strerror(errno), translated);
	} else {
		// We do not like open files!
		//::close(fd);
		auto f = this->file(translated, fi);
		f.file->type = open_file_t::FILE;
		f.file->flags = O_APPEND | O_RDWR;
		f.file->fh.fd = fd;
		f.file->is_open = true;
		f.file->has_changed = true;
	}

	return retstat;
}


int Module::utimens(const char *path, const struct timespec tv[2]) {
	this->trace("utimens", path);
	int retstat = 0;
	std::string translated;
	if ((retstat = this->translatepath(path, translated))) {
		this->info("utimens", "translatepath failed", path);
		return retstat;
	}

	retstat = ::utimensat(0, translated.c_str(), tv, 0);
	if (retstat < 0) {
		retstat = -errno;
		this->warn("utimens", "utimesat", translated);
	}

	return retstat;
}

}
