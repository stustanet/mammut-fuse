#pragma once

#include <queue>
#include <mutex>
#include <condition_variable>

#include <sys/eventfd.h>
#include <unistd.h>

// A threadsafe-queue.
template <class T>
class SafeQueue {
public:
	SafeQueue():
	eventid(eventfd(0, EFD_CLOEXEC)) {}
	~SafeQueue(void) {}

	// Add an element to the queue.
	void enqueue(const T &t) {
		std::unique_lock<std::mutex> lock(mutex);
		queue.push(t);

		int64_t val = queue.size();
		int retval = write(eventid, &val, sizeof(val));
		if (retval < 0) {
			perror("eventfd write");
			return;
		}

		cond.notify_one();
	}

	// Get the "front"-element.
	// If the queue is empty, wait till a element is avaiable.
	bool dequeue(T& val, bool wait = true) {
		std::unique_lock<std::mutex> lock(mutex);
		while(queue.empty()) {
			if(!wait) return false;
			// release lock as long as the wait and reaquire it afterwards.
			cond.wait(lock);
		}
		val = queue.front();
		queue.pop();
		return true;
	}

	inline int get_eventfd() const {
		return eventid;
	}

	bool empty() {
		return queue.empty();
	}
private:
	int eventid;
	std::queue<T> queue;
	mutable std::mutex mutex;
	std::condition_variable cond;
};
