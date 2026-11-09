#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <string>
#include <string_view>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <chrono>
#include <sstream>
#include <algorithm>


//AddNumChecker ensures a that the counters for the threads do not go beyond that established range for this project
namespace AddNumChecker {

	//int32_t will use only signed 32 bit integers, which will enforce type security
	constexpr std::int32_t MIN_COUNT = 0;
	constexpr std::int32_t MAX_COUNT = 20;

	//If statements to catch out-of-bound numbers in either direction
	constexpr std::int32_t addNums (std::int32_t value, std::int32_t delta) {
		if (delta > 0 && value > std::numeric_limits<std::int32_t>::max() - delta) {
			throw std::overflow_error("ERROR: overflow detected!");
		}

		if (delta <0 && value < std::numeric_limits <std::int32_t > ::min() - delta) {
			throw std::underflow_error("ERROR: underflow detected!");
		}

		return value + delta;
	}

}

//StringChecker uses pointers and explicitly stated lengths to avoid null-termination reliance
namespace StringChecker {

	//MAX_LABEL_LEN protects against denial of service log exhaustion, log injections, and null-termination by enforcing a 32 character max length
	constexpr std::size_t MAX_LABEL_LEN = 32;

	std::string sanitizeLabel(std::string_view rawLabel) {
		std::string clean;
		const std::size_t boundedLen = std::min(rawLabel.size(), MAX_LABEL_LEN);
		clean.reserve(boundedLen);

		for(std::size_t i = 0; i < boundedLen; ++i) {
			const unsigned char c = static_cast<unsigned char>(rawLabel[i]);
			if (c >= 32 && c <= 126 && c != '%'){
				clean.push_back(static_cast<char>(c));
			}
		}
		return clean;
	}
}

class ConcurrencyCounter {

private:
	std::mutex mtx_;
	std::condition_variable cv_;
	std::int32_t sharedCounter_{AddNumChecker::MIN_COUNT};
	bool countUpCompleted_{false};
	bool abortFlag_{false};

public:

	ConcurrencyCounter()= default;
	ConcurrencyCounter(const ConcurrencyCounter&) = delete;
	ConcurrencyCounter& operator =(const ConcurrencyCounter&) = delete;

	//Thread 1 counting 0 to 20, break called after 20 is reached
	void countUp(std::string_view label) {
		const std::string cleanLabel = StringChecker::sanitizeLabel(label);
		std::ostringstream localLog;

		try{
			{
				std::lock_guard<std::mutex> lock(mtx_);
				sharedCounter_ = AddNumChecker::MIN_COUNT;

				while (sharedCounter_ <= AddNumChecker::MAX_COUNT){
					localLog <<"[" << cleanLabel << "] counting up: " <<sharedCounter_ << "\n";
					if (sharedCounter_ == AddNumChecker::MAX_COUNT){
						break;
					}
					sharedCounter_ = AddNumChecker::addNums(sharedCounter_,1);
				}
			//flushing buffered output before unlocking
			std::cout <<localLog.str();
			countUpCompleted_ = true;
			}

			cv_.notify_one();
		} catch (const std::exception& ex){
		std::lock_guard<std::mutex> lock(mtx_);
		abortFlag_ = true;
		std::cerr <<"[ERROR in " << cleanLabel<< "]: " <<ex.what()<< "\n";

		cv_.notify_all();
		}
	}


	//Thread 2 waits for thread 1 to count to 20 before counting from 20 back down to 0, calling break at 0
	 void countDown(std::string_view label){
		 const std::string cleanLabel = StringChecker::sanitizeLabel(label);
		 std::ostringstream localLog;

		 try{
			 std::unique_lock<std::mutex> lock(mtx_);

			 const bool ready = cv_.wait_for(
					 lock,
					 std::chrono::seconds(5),
					 [this]{return countUpCompleted_ || abortFlag_;}
			 );

			 if(!ready) {
				 throw std::runtime_error("[ERROR]: action timed out waiting for thread one to complete!");
			 }
			 if (abortFlag_){
				 throw std::runtime_error("[ERROR]: aborted because thread one encountered an error!");
			 }

			 while(sharedCounter_ >= AddNumChecker::MIN_COUNT){
				 localLog << "[" <<cleanLabel<< "] counting down: " <<sharedCounter_ << "\n";
				 if (sharedCounter_ == AddNumChecker::MIN_COUNT){
					 break;
				 }
				 sharedCounter_ = AddNumChecker::addNums(sharedCounter_, -1);
			 }

			 std::cout << localLog.str();
		 } catch(const std::exception& ex) {
			 std::cerr <<"[ERROR in " <<cleanLabel << "]: " <<ex.what()<< "\n";
		 }
	}
};

//standard scoped thread class for RAII
class TargetThread{

private:
	std::thread thread_;

public:
	explicit TargetThread(std::thread t): thread_(std::move(t)) {

		if(!thread_.joinable()) {
			throw std::logic_error("No active thread was passed the ThreadJoiner!");
		}
	}

	~TargetThread() noexcept{
		if(thread_.joinable()){
			thread_.join();
		}
	}

	TargetThread(const TargetThread&) = delete;
	TargetThread& operator=(const TargetThread&) = delete;
};

int main() {
	try{
		std::cout << "Hello worl...Concurrency Counter!\n";

		ConcurrencyCounter counter;

		//initializing thread 2 first as validation of the concurrency requirement
		TargetThread t2(std::thread(&ConcurrencyCounter::countDown, &counter, "Thread 2 "));
		TargetThread t1(std::thread(&ConcurrencyCounter::countUp, &counter, "Thread 1 "));
	} catch(const std::exception& e) {
		std::cerr<<"[FATAL ERROR]: " << e.what()<< "\n";
		return 1;
	}

	std::cout << "Tah-dah!\n";
	return 0;
}






