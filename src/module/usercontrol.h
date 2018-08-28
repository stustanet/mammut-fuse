#pragma once

#include "../filemodule.h"

#include "../mammut_config.h"
#include "../communicator.h"

namespace mammutfs {

/**
 * A configfile located in / for the mammutfs, whereby users can set their own
 * config options, for example their display name in public listing
 **/
class UserControl : public FileModule {
public:
	UserControl (const std::shared_ptr<MammutConfig> &config,
	             const std::shared_ptr<Communicator> &comm) :
		FileModule("control", config, comm) {

		this->config->init_userconfig = [this]()
		{
			if (this->config->userconfig.size() != 0) return;
			// TODO: make sure that it will not block during startup, as was done
			// in the ugly 2018-08-27 bug
			this->changed();
		};
	}

	void make_default() override {
		{
			std::string out;
			this->translatepath("/", out);
			std::ofstream file(out, std::ios::out);

			file << "# You can use # for comments like this line" << std::endl
			     << "# Configuration is set one option per line as " << std::endl
			     << "# option=value" << std::endl
			     << "# Whenever this file is saved it is re-read by mammut." << std::endl
			     << "# If this file is emptied it will be replaced by the "
			        "default config again" << std::endl
			     << std::endl
			     << "# Displayname is the name used as your public folder in "
			        "the public folder." << std::endl
			     << "displayname=" <<  this->config->username() << std::endl;
			// TODO add a few more config values
		}
		this->changed();
	}

	void changed() override {
		std::string out;
		this->translatepath("/", out);


		std::stringstream errorfile;
		bool error = false;
		auto userconfig = std::unordered_map<std::string, std::string>();

		std::ifstream file(out, std::ios::in);
		if (!file) {
			this->warn("scan", "error opening config ` file ", out);
			return;
		}

		userconfig.clear();
		std::string line;
		while(std::getline(file, line, '\n')) {
			if (line.size() != 0) {
				//Trim spaces on the left
				// c++17 version ...
				//line.remove_prefix(std::min(line.find_first_not_of("\t "),
				//                            line.size()));

				// old version
				line.erase(0, std::min(line.find_first_not_of("\t "), line.size()));

				// remove comments
				if (line[0] != '#') {
					size_t split = line.find('=');
					if (split == std::string::npos) {
						this->info("scan", "Skipping invalid line: ", line);
						errorfile << "# ERROR: The next line is invalid and"
						             " will be ignored" << std::endl;
						errorfile << "# ";
						error = true;
					} else {
						userconfig[line.substr(0, split)] = line.substr(split+1);
					}
				}
			}
			errorfile << line << "\n";
		}
		file.close();

		// Displayname was not configured
		if (userconfig.find("displayname") == userconfig.end()) {
			errorfile << "# WARNING: displayname was unconfigured." << std::endl;
			errorfile << "displayname=" << this->config->username();
			error = true;

			userconfig["displayname"] = this->config->username();
		}

		auto displaynameit = config->userconfig.find("displayname");
		if(displaynameit == config->userconfig.end()) {
			// The displayname was even originally not inserted - only happens at
			// first startup, so we do not notify the daemon
			this->config->userconfig["displayname"] = this->config->username();
		} else if (displaynameit->second != userconfig["displayname"]) {
			// it was already there - and has changed!

			// notify mammutfsd to re-read public listing
			std::stringstream ss;
			ss << "{\"event\":\"namechange\","
			      "\"source\":\"" << displaynameit->second << "\","
			      "\"dest\":\"" << userconfig["displayname"] << "\"}\n";

			std::cout << "changed username: " << ss.str();
			this->comm->send(ss.str());

			displaynameit->second = userconfig["displayname"];
		}

		if (error) {
			std::ofstream file(out, std::ios::out | std::ios::trunc);
			if (!file) {
				this->warn("scan", "error opening config ` file ", out);
				return;
			}
			file << errorfile.str();
		}
	}
};

}
