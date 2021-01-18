#pragma once

#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace edoren {

template <typename T>
using ResolveCallback = std::function<void(const T& value)>;

using RejectCallback = std::function<void(const std::string& value)>;

template <typename T>
using FinallyCallback = std::function<void(void)>;

template <typename T>
class Promise;

template <typename T>
struct IsPromise : public std::false_type {};

template <typename T>
struct IsPromise<Promise<T>> : public std::true_type {};

template <typename T>
class Promise {
public:
    template <typename U>
    friend class Promise;

    enum class Status { RESOLVED, REJECTED, ONGOING };

private:
    class SharedState {
    public:
        void resolve(const T& value) {
            std::lock_guard<std::mutex> lock(m_fulfilledMutex);
            if (m_status == Promise::Status::ONGOING) {
                m_status = Promise::Status::RESOLVED;
                m_value = value;
                for (auto& callback : m_resolveCallbacks) {
                    callback(value);
                }
                for (auto& callback : m_finallyCallbacks) {
                    callback();
                }
                m_resolveCallbacks.clear();
                m_finallyCallbacks.clear();
                m_signaler.notify_all();
            } else {
                // ERROR: Promise already fulfilled
            }
        }

        void reject(std::string_view error) {
            std::lock_guard<std::mutex> lock(m_fulfilledMutex);
            if (m_status == Promise::Status::ONGOING) {
                m_status = Promise::Status::REJECTED;
                m_error.assign(error.begin(), error.end());
                for (auto& callback : m_rejectCallbacks) {
                    callback(m_error);
                }
                for (auto& callback : m_finallyCallbacks) {
                    callback();
                }
                m_rejectCallbacks.clear();
                m_finallyCallbacks.clear();
                m_signaler.notify_all();
            } else {
                // ERROR: Promise already fulfilled
            }
        }

        Promise::Status getStatus() const {
            return m_status;
        }

        const T& getValue() const {
            return m_value;
        }

        const std::string& getError() const {
            return m_error;
        }

        std::mutex& getMutex() {
            return m_fulfilledMutex;
        }

        void appendResolveCallback(ResolveCallback<T>&& callback) {
            m_resolveCallbacks.push_back(std::move(callback));
        }

        void appendRejectCallback(RejectCallback&& callback) {
            m_rejectCallbacks.push_back(std::move(callback));
        }

        void appendFinallyCallback(FinallyCallback<T>&& callback) {
            m_finallyCallbacks.push_back(std::move(callback));
        }

        void wait() {
            if (m_status == Promise::Status::ONGOING) {
                std::unique_lock<std::mutex> lock(m_signalMutex);
                m_signaler.wait(lock);
            }
        }

    private:
        Promise::Status m_status = Promise::Status::ONGOING;
        T m_value;
        std::string m_error;
        std::mutex m_fulfilledMutex;

        std::condition_variable m_signaler;
        std::mutex m_signalMutex;

        std::vector<ResolveCallback<T>> m_resolveCallbacks;
        std::vector<RejectCallback> m_rejectCallbacks;
        std::vector<FinallyCallback<T>> m_finallyCallbacks;
    };

public:
    template <typename Func, typename = std::enable_if_t<!IsPromise<std::decay_t<Func>>::value>>
    Promise(Func&& executor) {
        // std::cout << "Creating 1" << std::endl;
        m_shared = std::make_shared<SharedState>();
        auto resolveFn = [shared = m_shared](const T& value) { shared->resolve(value); };
        auto rejectFn = [shared = m_shared](std::string_view reason) { shared->reject(reason); };
        // static_assert(std::is_invocable<decltype(executor), decltype(resolveFn), decltype(rejectFn)>::value,
        //               "Executor provider executor should accept a resolve and reject function, "
        //               "please use: [](auto&& resolve, auto&& reject) {}");
        executor(std::move(resolveFn), std::move(rejectFn));
    }

    using ValueType = T;
    using SharedType = SharedState;

    Promise(const Promise& other) = default;

    Promise(Promise&& other) noexcept = default;

    static Promise Resolve(const T& value) {
        // std::cout << "Resolve new" << std::endl;
        auto newShared = std::make_shared<SharedState>();
        newShared->resolve(value);
        return Promise(newShared);
    }

    static Promise Reject(std::string_view reason) {
        // std::cout << "Reject new" << std::endl;
        auto newShared = std::make_shared<SharedState>();
        newShared->reject(reason);
        return Promise(newShared);
    }

    template <typename Func,
              typename PromiseRetType =
                  std::enable_if_t<(std::is_void_v<std::invoke_result_t<Func, const T&>> ||
                                    IsPromise<std::invoke_result_t<Func, const T&>>::value),
                                   std::conditional_t<std::is_void_v<std::invoke_result_t<Func, const T&>>,
                                                      Promise,
                                                      std::invoke_result_t<Func, const T&>>>>
    auto then(Func&& func) const -> PromiseRetType {
        using FuncRetType = std::invoke_result_t<Func, const T&>;

        static_assert(IsPromise<PromiseRetType>::value, "Promise execution should return another promise or void");

        if (!m_shared || m_shared->getStatus() == Promise::Status::REJECTED) {
            if constexpr (std::is_void_v<FuncRetType>) {
                return *this;
            } else {
                if constexpr (std::is_same_v<ValueType, typename PromiseRetType::ValueType>) {
                    return *this;
                } else {
                    if (!m_shared) {
                        return PromiseRetType::Reject("Non-initialized promise");
                    }
                    return PromiseRetType::Reject(m_shared->getError());
                }
            }
        }

        std::lock_guard<std::mutex> lock(m_shared->getMutex());

        if (m_shared->getStatus() == Promise::Status::RESOLVED) {
            if constexpr (std::is_void_v<FuncRetType>) {
                func(m_shared->getValue());
                return *this;
            } else {
                return func(m_shared->getValue());
            }
        } else if (m_shared->getStatus() == Promise::Status::ONGOING) {
            if constexpr (std::is_void_v<FuncRetType>) {
                // std::cout << "Ongoing (void) new" << std::endl;
                auto newShared = std::make_shared<SharedState>();

                m_shared->appendResolveCallback([func, newShared](auto& value) {
                    func(value);
                    newShared->resolve(value);  //
                });
                m_shared->appendRejectCallback([newShared](auto& error) {
                    newShared->reject(error);  //
                });

                return Promise(newShared);
            } else {
                // std::cout << "Ongoing (Promise) new" << std::endl;
                auto newShared = std::make_shared<typename PromiseRetType::SharedType>();

                m_shared->appendResolveCallback([func, newShared](auto& value) {
                    PromiseRetType other = func(value);
                    other.then([newShared](auto& value) { newShared->resolve(value); });
                    other.failed([newShared](auto& error) { newShared->reject(error); });
                });
                m_shared->appendRejectCallback([newShared](auto& error) {
                    newShared->reject(error);  //
                });

                return PromiseRetType(newShared);
            }
        }

        // ERROR This should not happen as all the cases are covered
        return PromiseRetType::Reject("Non-initialized promise");
    }

    template <typename Func>
    auto failed(Func&& func) -> Promise& {
        if (!m_shared) {
            func("Promise has been moved");
            return *this;
        }

        if (m_shared->getStatus() == Promise::Status::REJECTED) {
            func(m_shared->getError());
        } else if (m_shared->getStatus() == Promise::Status::ONGOING) {
            m_shared->appendRejectCallback([func](auto& error) { func(error); });
        }

        return *this;
    }

    template <typename Func>
    auto finally(Func&& func) -> Promise& {
        if (!m_shared) {
            return *this;
        }

        static_assert(std::is_void_v<std::invoke_result_t<Func>>, "Promise finally callback should return void");

        if (m_shared->getStatus() != Promise::Status::ONGOING) {
            func();
        } else {
            m_shared->appendFinallyCallback([func]() { func(); });
        }

        return *this;
    }

    void wait() {
        if (m_shared) {
            m_shared->wait();
        }
    }

private:
    Promise(std::shared_ptr<SharedState> state) : m_shared(std::move(state)) {}

    std::shared_ptr<SharedState> m_shared;
};

}  // namespace edoren
