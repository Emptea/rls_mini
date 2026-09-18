#ifndef RX_ORDER_HPP
#define RX_ORDER_HPP

#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <vector>

// One complete receive/DMA iteration per active board, in registration order.
class RxOrder {
public:
	explicit RxOrder(size_t count): active(count, true) {}

	void start() {
		std::lock_guard<std::mutex> lock(mutex);
		ready = true;
		changed.notify_all();
	}

	bool wait(size_t index) {
		std::unique_lock<std::mutex> lock(mutex);
		changed.wait(lock, [this, index] { return cancelled || (ready && turn == index); });
		return !cancelled;
	}

	void finish(size_t index, bool done) {
		std::lock_guard<std::mutex> lock(mutex);
		active[index] = !done;
		for (size_t offset = 1; offset <= active.size(); ++offset) {
			const size_t next = (index + offset) % active.size();
			if (active[next]) {
				turn = next;
				changed.notify_all();
				return;
			}
		}
		cancelled = true;
		changed.notify_all();
	}

	void cancel() {
		std::lock_guard<std::mutex> lock(mutex);
		cancelled = true;
		changed.notify_all();
	}

private:
	std::mutex mutex;
	std::condition_variable changed;
	std::vector<bool> active;
	size_t turn    = 0;
	bool ready     = false;
	bool cancelled = false;
};

#endif // RX_ORDER_HPP
