#pragma once

#include "thread_queue.h"

#include <unordered_map>
#include <functional>
#include <thread>
#include <memory>
#include <sstream>


namespace mammutfs {

class MammutConfig;


/** Unix Socket listener/sender
 *
 * Uses the unix socket defined in ${config:mammutfs_config_socketdir}/${config:username}
 * Sends iNotify messages to this socket via the inotify command in the
 * structure of:
 *
 *     { "op":"CREATE|MODIFY|DELETE", "path":"${PATH}" }
 *
 * Receives commands in the format of
 *
 *     COMMAND:DATA
 * or
 *     COMMAND
 *
 * whereby the given callback is called and additional data is passed as string
 *
 */
class Communicator {
public:
	Communicator (std::shared_ptr<MammutConfig> config);

	virtual ~Communicator();

	/* Start the threads */
	void start();

	void send(const std::string &data);
	void inotify (const std::string &operation,
	              const std::string &module,
	              const std::string &path,
	              const std::string &path2 = "");

	using command_callback = std::function<bool(const std::string &data,
	                                            std::string &)>;
	void register_command(const std::string &command,
	                      const command_callback &,
	                      const std::string &helptext = ""
	);

	using void_command_callback = std::function<void(const std::string &data,
	                                                 std::string &resp)>;
	void register_void_command(const std::string &command,
	                           const void_command_callback &cb,
	                           const std::string &helptext = "") {
		register_command(command,
		                 [cb](const std::string &data, std::string &resp) {
			                 cb(data, resp);
			                 return true;
		                 }, helptext);
	}


private:
	bool connect(bool initial_attempt);

	void communication_thread();

	void receive_command();
	void send_command(const std::string &data);

	void execute_command(std::string cmd);

	std::shared_ptr<MammutConfig> config;
	std::string socketname;
	int socket;
	volatile bool connected;

	std::unique_ptr<std::thread> thrd_comm;
	bool running;

	struct command {
		command_callback callback;
		std::string helptext;
	};
	std::unordered_map<std::string, command> commands;

	SafeQueue<std::string> queue;

	std::stringstream sstrbuf;
};
}
