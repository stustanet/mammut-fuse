#pragma once

#include "module.h"

#include <string.h>

#include <map>
#include <memory>
#include <unordered_map>

namespace mammutfs {

class Module;


/**
 * Resovles a module name, configurable via activation.
 * At the beginning of main() modules get registered. These can be activated
 * seperately afterwards (for example using a config file)
 */
class ModuleResolver {
public:
	/**
	 * resolve a module from name to module
	 */
	Module *getModule(const char *alias, int strlen = -1) const {
		std::string search = (strlen >= 0)?
		                     std::string(alias, strlen) :
		                     std::string(alias);

		auto it = activated.find(search);
		if (it == activated.end()) {
			return nullptr;
		}
		return it->second;
	}

	/** extract the module from a path, store the remaining path and return the
	 * module
	 */
	Module *getModuleFromPath (const char *path, const char *&remaining_path) {
		// If we only have a single module, we map it directly through
		if (this->is_single_module()) {
			remaining_path = path;
			return this->get_single_module();
		}

		int len = strlen(path);
		if (len <= 1) {
			remaining_path = path;
			return getModule("default");
		}

		const char *p = strchr(path+1, '/');

		if (p == NULL) {
			p = path + len; // point to the \0 character
			remaining_path = "/";
		} else {
			remaining_path = p;
		}
		size_t mlen = (p - path - 1);
		return getModule(path + 1, mlen);
	}


	/**
	 * Register a module with the name
	 */
	void registerModule(const char *alias, std::shared_ptr<Module> module) {
		registered[alias] = module;
	}

	/**
	 * activate a registered module.
	 */
	bool activateModule(const std::string &key) {
		auto it = registered.find(key);
		if (it != registered.end()) {
			activated.insert(std::make_pair(it->first, it->second.get()));
			return true;
		} else {
			return false;
		}
	}

	bool is_single_module() {
		return this->activated.size() == 2; // default and XX
	}

	Module *get_single_module() {
		for (const auto &m : this->activated) {
			if (m.first != "default") {
				return m.second;
			}
		}
		return nullptr;
	}

	const std::map<std::string, Module *> &activatedModules() const {
		return activated;
	}
private:
	std::unordered_map<std::string, std::shared_ptr<Module>> registered;
	/**
	 * This is an _ordered_ map by design, so that the order of modules at the home page
	 * is always the same even in programms that do not sort the output of the directory
	 * filler
	 */
	std::map<std::string, Module *> activated;
};

}
