#pragma once

#include "thread_queue.h"

#include <map>
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

	/* Start the threads */
	void start();

	virtual ~Communicator();

	void send(const std::string &data);
	void inotify (const std::string &operation, const std::string &path, const std::string &path2 = "");

	using command_callback = std::function<bool(const std::string &data)>;
	void register_command(const std::string &command,
	                      const command_callback &,
	                      const std::string &helptext = ""
	);

	using void_command_callback = std::function<void(const std::string &data)>;
	void register_void_command(const std::string &command,
	                           const void_command_callback &cb,
	                           const std::string &helptext = "") {
		register_command(command,
		                 [cb](const std::string &data) {
			                 cb(data);
			                 return true;
		                 }, helptext);
	}


private:
	void thread_recv();
	void thread_send();

	void execute_command(std::string cmd);

	void remove_client(int socket);

	std::shared_ptr<MammutConfig> config;

	int connect_socket;
	int pollingfd;
	std::vector<int> connected_sockets;

	struct command {
		command_callback callback;
		std::string helptext;
	};
	std::map<std::string, command> commands;

	SafeQueue<std::string> queue;

	std::unique_ptr<std::thread> thrd_send;
	std::unique_ptr<std::thread> thrd_recv;

	bool running;

	std::stringstream sstrbuf;
};
}
