#pragma once

#include <libconfig.h++>

#include <list>
#include <map>
#include <memory>
#include <string>
#include <sstream>
#include <iostream>

namespace libconfig {
class Config;
}

namespace mammutfs {

class ModuleResolver;

class MammutConfig {
public:
	MammutConfig (const char *filename,
	              int argc,
	              char **argv,
	              std::shared_ptr<ModuleResolver> resolver);

	void filterModules(std::shared_ptr<ModuleResolver> mods);

	std::shared_ptr<ModuleResolver> resolver;

	std::list<std::string> raids;
	bool deamonize;
	uint64_t truncate_max_size;
	std::string anon_username;
	std::string username;
	std::string mountpoint;
	std::string anon_mapping_file;

	uint16_t anon_uid;
	uint16_t anon_gid;
	uint16_t user_uid;
	uint16_t user_gid;

	char *const self;


	template <typename _T>
	bool lookupValue(const char *key, _T &value) const {
		std::cout << "Looking for " << key;
		auto it = cmdline.find(key);
		if (it != cmdline.end()) {
			buf.clear();
			buf.str(it->second);
			buf >> value;
			std::cout << " [cmd] " << value << std::endl;
			return true;
		} else {
			const char *tmp;
			bool state = config->lookupValue(key, tmp);
			if (state) {
				buf.clear();
				buf.str(tmp);
				buf >> value;
				std::cout << " [file] " << value << std::endl;
			} else {
				std::cout << " \033[044mCOULD NOT FIND CONFIG VALUE!\033[00m" << std::endl;
				exit(-1);
			}
			return state;
		}
	}

	// todo database shit
private:
	mutable std::istringstream buf;
	std::map<std::string, std::string> cmdline;
	std::shared_ptr<libconfig::Config> config;
};

}
