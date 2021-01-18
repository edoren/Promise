#include <chrono>
#include <thread>

#include <catch2/catch.hpp>

#include <Promise.hpp>

using namespace edoren;

template <typename CallbackFn, typename Val>
std::thread&& AsyncTask(CallbackFn&& callback, Val&& value, long double duration = 0.25) {
    return std::move(std::thread([callback, value, duration] {
        std::this_thread::sleep_for(std::chrono::duration<long double>(duration));
        callback(value);
    }));
}

TEST_CASE("Promise constructor should yield a value when resolved") {
    int result;
    auto prom = Promise<int>([](auto&& resolve, auto&& reject) { resolve(10); });
    prom.then([&result](const int& val) { result = val; });
    REQUIRE(result == 10);
}

TEST_CASE("Promise::Resolve should yield a value when resolved") {
    std::string result;
    auto prom = Promise<std::string>::Resolve("Hello World");
    prom.then([&result](const std::string& val) { result = val; });
    REQUIRE(result == "Hello World");
}

TEST_CASE("Promise::Reject should fail the promise with a reason") {
    std::string result;
    auto prom = Promise<std::string>::Reject("Failed");
    prom.then([&result](const std::string&) { result = "Hello World"; });
    prom.failed([&result](const std::string& val) { result = val; });
    REQUIRE(result == "Failed");
}

TEST_CASE("Promise::finally should be called after the promise is fulfilled") {
    SECTION("When the Promise is resolved") {
        std::string result;
        auto prom = Promise<std::string>::Resolve("LOREM");
        prom.finally([&result]() { result = "HELLO"; });
        REQUIRE(result == "HELLO");
    }
    SECTION("When the Promise is rejected") {  // TODO
        std::string result;
        auto prom = Promise<std::string>::Reject("LOREM");
        prom.finally([&result]() { result = "WORLD"; });
        REQUIRE(result == "WORLD");
    }
}

TEST_CASE("Promise::then should be called after the promise is fulfilled") {
    SECTION("When the Promise is resolved synchronously") {
        std::string result;
        auto prom = Promise<std::string>::Resolve("LOREM");
        prom.finally([&result]() { result = "HELLO"; });
        REQUIRE(result == "HELLO");
    }
    SECTION("When the Promise is rejected synchronously") {  // TODO
        std::string result;
        auto prom = Promise<std::string>::Reject("LOREM");
        prom.finally([&result]() { result = "WORLD"; });
        REQUIRE(result == "WORLD");
    }
    SECTION("When the Promise is resolved asynchronously") {
        std::string result;
        std::thread t;

        auto prom = Promise<std::string>([&t](auto&& resolve, auto&& reject) {
                        t = AsyncTask(resolve, "123");
                    }).finally([&result]() { result = "WORLD"; });
        result = "HELLO";
        t.join();

        REQUIRE(result == "WORLD");
    }
    SECTION("When the Promise is rejected asynchronously") {
        std::string result;
        std::thread t;

        auto prom = Promise<std::string>([&t](auto&& resolve, auto&& reject) {
                        t = AsyncTask(reject, "123");
                    }).finally([&result]() { result = "WORLD"; });
        result = "HELLO";
        t.join();

        REQUIRE(result == "WORLD");
    }
}
