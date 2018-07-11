#include "mammut_config.h"

#include "resolver.h"

#include <libconfig.h++>

#include <iostream>
#include <cstring>

#include <sys/types.h>
#include <pwd.h>

namespace mammutfs {

MammutConfig::MammutConfig(const char *filename,
                           int argc,
                           char **argv,
                           const std::shared_ptr<ModuleResolver> &resolver) :
	resolver{resolver},
	self(argv[0]),
	config {std::make_shared<libconfig::Config>()} {
	try {
		config->readFile(filename);
	} catch (libconfig::FileIOException &e) {
		std::cerr << "Config not found: " << filename << ": " << e.what()
			<< std::endl;
		exit(-1);
	}

	enum STATE {
		EXPECT_KEY,
		EXPECT_VALUE
	};
	STATE state = EXPECT_KEY;
	const char *last_key;
	for (int i = 1; i < argc; ++i) {
		if (state == EXPECT_KEY) {
			if (argv[i][0] == '-' && argv[i][1] == '-') {
				last_key = &argv[i][2];
				//cmdline[last_key] = "";
				state = EXPECT_VALUE;
			} else {
				std::cout << "Error in command Line: Expecting option, "
				          << "got: " << argv[i] << std::endl;
			}
		} else if (state == EXPECT_VALUE) {
			cmdline[last_key] = std::string(argv[i]);
			state = EXPECT_KEY;
		}
	}

	if (!cmdline.empty()) {
		std::cout << "CMD Line: " << std::endl;
		for (const auto &e : this->cmdline) {
			std::cout << "\t" << e.first << "=" << e.second << std::endl;
		}
		std::cout << std::endl;
	}

	auto &settings_raid = config->lookup("raids");
	for (auto it = settings_raid.begin(); it != settings_raid.end(); ++it) {
		raids.push_back(it->c_str());
	}

	// access all values once, so the startup will fail if a value is missing
	this->mountpoint();
	this->deamonize();
	this->truncate_max_size();
	this->anon_username();
	this->anon_mapping_file();
	this->username();

	this->update_anonuser();
	this->update_user();

	this->register_changeable("anon_username", [this](){
			this->update_anonuser();
		});
}

void MammutConfig::filterModules(std::shared_ptr<ModuleResolver> mods) {
	const auto& modulelist = this->config->lookup("modules");
	std::stringstream ss;
	ss << "Activating modules: ";
	for (auto it = modulelist.begin(); it != modulelist.end(); ++it) {
		std::string module = *it;
		if (mods->activateModule(module)) {
			ss << module << ", ";
		} else {
			std::cerr << "ERROR: Module not found: " << module << std::endl;
			exit(-1);
		}
	}
	std::cout << ss.str() << std::endl;
}

void MammutConfig::update_anonuser() {
	struct passwd *passwd_info = getpwnam(anon_username().c_str());
	if (passwd_info == NULL) {
		std::cerr << "Could not find anonymous user \"" << anon_username()
		          << "\"" << std::endl;
		exit(-1);
	}
	anon_uid = passwd_info->pw_uid;
	anon_gid = passwd_info->pw_gid;
}

void MammutConfig::update_user() {
	struct passwd *passwd_info = getpwnam(username().c_str());
	if (passwd_info == NULL) {
		std::cerr << "Could not find user \"" << username()
		          << "\"" << std::endl;
		exit(-1);
	}
	user_uid = passwd_info->pw_uid;
	user_gid = passwd_info->pw_gid;

	std::cout << "Operating User: " << username()
	          << " UID " << user_uid
	          << " GID " << user_gid << std::endl;
}
}
