#include <iostream>
#include <thread>

#include <curl/curl.h>

#include <Promise.hpp>

using namespace edoren;
using namespace std::chrono_literals;

size_t writeFunction(void* ptr, size_t size, size_t nmemb, std::string* data) {
    data->append((char*)ptr, size * nmemb);
    return size * nmemb;
}

int main(int argc, const char* argv[]) {
    std::thread t;

    auto prom = Promise<std::string>([&t](auto&& resolve, auto&& reject) {
                    t = std::thread([resolve, reject] {
                        auto curl = curl_easy_init();

                        curl_easy_setopt(curl, CURLOPT_URL, "https://edoren.me");
                        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 1L);
                        curl_easy_setopt(curl, CURLOPT_USERPWD, "user:pass");
                        curl_easy_setopt(curl, CURLOPT_USERAGENT, "curl/7.42.0");
                        curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 50L);
                        curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);

                        std::string response_string;
                        std::string header_string;

                        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeFunction);
                        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_string);
                        curl_easy_setopt(curl, CURLOPT_HEADERDATA, &header_string);

                        curl_easy_perform(curl);

                        char* url;
                        long response_code;
                        double elapsed;
                        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
                        curl_easy_getinfo(curl, CURLINFO_TOTAL_TIME, &elapsed);
                        curl_easy_getinfo(curl, CURLINFO_EFFECTIVE_URL, &url);

                        curl_easy_cleanup(curl);
                        std::cout << "REQUEST FINISHED WITH RESPONSE CODE " << response_code << std::endl;

                        resolve(response_string);
                    });
                })
                    .then([](const std::string& value) {
                        std::cout << value << std::endl;
                        // return Promise<long>::Resolve(10);
                        return Promise<long>::Reject("FAILED");
                    });

    std::cout << "HELLO REQUEST" << std::endl;

    t.join();

    return 0;
}
