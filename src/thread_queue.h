#pragma once

#include <queue>
#include <mutex>
#include <condition_variable>

// A threadsafe-queue.
template <class T>
class SafeQueue
{
public:
	SafeQueue() {}

	~SafeQueue(void) {}

	// Add an element to the queue.
	void enqueue(const T &t) {
		std::lock_guard<std::mutex> lock(mutex);
		queue.push(t);
		cond.notify_one();
	}

	// Get the "front"-element.
	// If the queue is empty, wait till a element is avaiable.
	void dequeue(T* val) {
		std::unique_lock<std::mutex> lock(mutex);
		while(queue.empty())
		{
			// release lock as long as the wait and reaquire it afterwards.
			cond.wait(lock);
		}
		*val = queue.front();
		queue.pop();
	}

private:
	std::queue<T> queue;
	mutable std::mutex mutex;
	std::condition_variable cond;
};
