#pragma once

#include "config.h"

#include <libconfig.h++>

#include <sys/types.h>

#include <list>
#include <unordered_map>
#include <memory>
#include <string>
#include <sstream>
#include <iostream>
#include <functional>

namespace libconfig {
class Config;
}

namespace mammutfs {

class ModuleResolver;

class MammutConfig {
public:
	MammutConfig(const char *filename,
	              int argc,
	              char **argv,
	              const std::shared_ptr<ModuleResolver> &resolver);
	using changeable_callback = std::function<void(void)>;

	void register_changeable(const std::string &key, changeable_callback monitor) {
		changeables[key].push_back(monitor);
	}
	bool set_value(const std::string &key, const std::string& value) {
		auto it = changeables.find(key);
		if (it != changeables.end()) {
			// Change and notify
			manvalues[it->first] = value;
			for(const auto &f : it->second) {
				f();
			}
			return true;
		} else {
			return false;
		}
	}

	// Drop the value that was manually overwritten either via command or commandline
	// and read from configfile
	void unset_value(const std::string &key) {
		manvalues.erase(key);
	}

	void filterModules(std::shared_ptr<ModuleResolver> mods);

	std::shared_ptr<ModuleResolver> resolver;

	std::list<std::string> raids;
	bool deamonize() { return this->lookupValue<bool>("deamonize"); }
	int64_t truncate_max_size() { return this->lookupValue<int64_t>("truncate_maxsize"); }
	std::string anon_username() { return this->lookupValue<std::string>("anon_user_name"); }
	std::string username() { return this->lookupValue<std::string>("username"); }
	std::string mountpoint() { return this->lookupValue<std::string>("mountpoint"); }
	std::string anon_mapping_file() { return this->lookupValue<std::string>("anon_mapping_file"); }

	uid_t anon_uid;
	uid_t anon_gid;
	uid_t user_uid;
	uid_t user_gid;

	char *const self;

	template <typename _T>
	_T lookupValue(const char *key, bool ignore_error = false) {
		_T t;
		this->lookupValue(key, t, ignore_error);
		return t;
	}

	#ifndef ENABLE_CONFIG_DEBUG
	mutable std::stringstream voidstream;
	#endif
	std::iostream &log() const {
		#ifdef ENABLE_CONFIG_DEBUG
		return std::cout;
		#else
		return this->voidstream;
		#endif
	}

	template <typename _T>
	bool lookupValue(const char *key, _T &value, bool ignore_error = false) const {
		this->log() << "Looking for \"" << key << "\"";
		{
			auto it = manvalues.find(key);
			if (it != manvalues.end()) {
				buf.clear();
				buf.str(it->second);
				buf >> value;
				this->log() << " [man] " << value << std::endl;
				return true;
			}
		}
		{
			auto it = cmdline.find(key);
			if (it != cmdline.end()) {
				buf.clear();
				buf.str(it->second);
				buf >> value;
				this->log() << " [cmd] " << value << std::endl;
				return true;
			}
		}
		{
			const char *tmp;
			bool state = config->lookupValue(key, tmp);
			if (state) {
				buf.clear();
				buf.str(tmp);
				buf >> value;
				this->log() << " [file] " << value << std::endl;
			} else {
				std::cout << "\033[044mCOULD NOT FIND CONFIG VALUE:\033[00m "
				          << key << std::endl;
				if (!ignore_error) {
					exit(-1);
				}
			}
			return state;
		}
	}


	std::function<void()> init_userconfig;
	std::unordered_map<std::string, std::string> userconfig;

	// todo database shit
private:
	mutable std::istringstream buf;
	std::unordered_map<std::string, std::string> manvalues;
	std::unordered_map<std::string, std::string> cmdline;
	std::unordered_map<std::string, std::list<changeable_callback>> changeables;
	std::shared_ptr<libconfig::Config> config;

	void update_anonuser();
	void update_user();
};

}
