#include <Promise.hpp>

#include <iostream>
#include <thread>

using namespace edoren;

int main(int argc, const char* argv[]) {
    using namespace std::chrono_literals;

    std::thread thread, thread2;

    auto prom = Promise<int>([&thread](auto&& resolve, auto&& reject) {
                    thread = std::thread([resolve, reject] {
                        std::this_thread::sleep_for(1s);
                        resolve(123);
                    });
                })
                    .then([](const int& value) {
                        std::cout << "Int: " << value << std::endl;
                        // return Promise<long>::Resolve(10);
                        // return Promise<long>::Reject("FAILED");
                    })
                    .then([](const long& value) {
                        std::cout << "Long: " << value << std::endl;
                        return Promise<std::string>::Resolve("Hello World");
                        // return Promise<std::string>::Reject("FAILED");
                    })
                    .then([](const std::string& value) { std::cout << "Result: " << value << std::endl; })
                    .then([&thread2](const std::string& value) {
                        std::cout << "Result 2: " << value << std::endl;
                        return Promise<int>([&thread2](auto&& resolve, auto&& reject) {
                            thread2 = std::thread([resolve, reject] {
                                std::this_thread::sleep_for(1s);
                                reject("LOL");
                            });
                        });
                    })
                    .failed([](auto& error) { std::cout << "Error!!! - " << error << std::endl; })
                    .finally([] { std::cout << "Finished" << std::endl; });

    std::cout << "HELLO PROMISE" << std::endl;

    prom.wait();

    if (thread.joinable()) {
        thread.join();
    }

    if (thread2.joinable()) {
        thread2.join();
    }

    return 0;
}
