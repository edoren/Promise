#pragma once

#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace edoren {

template <typename Res, typename Rej = std::string>
class Promise;

template <typename T>
struct IsPromise : public std::false_type {};

template <typename Res, typename Rej>
struct IsPromise<Promise<Res, Rej>> : public std::true_type {};

template <typename Res, typename Rej>
class Promise {
public:
    template <typename ResU, typename RejV>
    friend class Promise;

    enum class Status { RESOLVED, REJECTED, ONGOING };

    using ResolveType = Res;
    using RejectType = Rej;

    using ResolveCallback = std::function<void(const ResolveType& value)>;
    using RejectCallback = std::function<void(const RejectType& value)>;
    using FinallyCallback = std::function<void(void)>;

private:
    class SharedState {
    public:
        void resolve(const ResolveType& value) {
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

        void reject(const RejectType& error) {
            std::lock_guard<std::mutex> lock(m_fulfilledMutex);
            if (m_status == Promise::Status::ONGOING) {
                m_status = Promise::Status::REJECTED;
                m_error = error;
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

        const ResolveType& getValue() const {
            return m_value;
        }

        const RejectType& getError() const {
            return m_error;
        }

        std::mutex& getMutex() {
            return m_fulfilledMutex;
        }

        void appendResolveCallback(ResolveCallback&& callback) {
            m_resolveCallbacks.push_back(std::move(callback));
        }

        void appendRejectCallback(RejectCallback&& callback) {
            m_rejectCallbacks.push_back(std::move(callback));
        }

        void appendFinallyCallback(FinallyCallback&& callback) {
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
        ResolveType m_value;
        RejectType m_error;
        std::mutex m_fulfilledMutex;

        std::condition_variable m_signaler;
        std::mutex m_signalMutex;

        std::vector<ResolveCallback> m_resolveCallbacks;
        std::vector<RejectCallback> m_rejectCallbacks;
        std::vector<FinallyCallback> m_finallyCallbacks;
    };

public:
    template <typename Func, typename = std::enable_if_t<!IsPromise<std::decay_t<Func>>::value>>
    Promise(Func&& executor) {
        // std::cout << "Creating 1" << std::endl;
        m_shared = std::make_shared<SharedState>();
        auto resolveFn = [shared = m_shared](const ResolveType& value) { shared->resolve(value); };
        auto rejectFn = [shared = m_shared](const RejectType& reason) { shared->reject(reason); };
        // static_assert(std::is_invocable<decltype(executor), decltype(resolveFn), decltype(rejectFn)>::value,
        //               "Executor provider executor should accept a resolve and reject function, "
        //               "please use: [](auto&& resolve, auto&& reject) {}");
        executor(std::move(resolveFn), std::move(rejectFn));
    }

    Promise(const Promise& other) = default;

    Promise(Promise&& other) noexcept = default;

    static Promise Resolve(const ResolveType& value) {
        // std::cout << "Resolve new" << std::endl;
        auto newShared = std::make_shared<SharedState>();
        newShared->resolve(value);
        return Promise(newShared);
    }

    static Promise Reject(const RejectType& reason) {
        // std::cout << "Reject new" << std::endl;
        auto newShared = std::make_shared<SharedState>();
        newShared->reject(reason);
        return Promise(newShared);
    }

    template <typename Func,
              typename PromiseRetType =
                  std::enable_if_t<(std::is_void_v<std::invoke_result_t<Func, const ResolveType&>> ||
                                    IsPromise<std::invoke_result_t<Func, const ResolveType&>>::value),
                                   std::conditional_t<std::is_void_v<std::invoke_result_t<Func, const ResolveType&>>,
                                                      Promise,
                                                      std::invoke_result_t<Func, const ResolveType&>>>>
    auto then(Func&& func) const -> PromiseRetType {
        using FuncRetType = std::invoke_result_t<Func, const ResolveType&>;

        static_assert(IsPromise<PromiseRetType>::value, "Promise execution should return another promise or void");
        static_assert(std::is_same_v<RejectType, PromiseRetType::RejectType>, "Promise RejectType should be the same");

        if (!m_shared || m_shared->getStatus() == Promise::Status::REJECTED) {
            if constexpr (std::is_void_v<FuncRetType>) {
                return *this;
            } else {
                if constexpr (std::is_same_v<ResolveType, PromiseRetType::ResolveType>) {
                    return *this;
                } else {
                    if (!m_shared) {
                        // Error: Non-initialized promise
                        if constexpr (std::is_constructible_v<RejectType, std::string_view>) {
                            return PromiseRetType::Reject(RejectType("Non-initialized promise"));
                        } else {
                            return PromiseRetType::Reject(RejectType());
                        }
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
                auto newShared = std::make_shared<PromiseRetType::SharedState>();

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

        // ERROR: This should not happen as all the cases are covered
        if constexpr (std::is_constructible_v<RejectType, std::string_view>) {
            return PromiseRetType::Reject(RejectType("Non-initialized promise"));
        } else {
            return PromiseRetType::Reject(RejectType());
        }
    }

    template <typename Func>
    auto failed(Func&& func) -> Promise& {
        if (!m_shared) {
            auto reason = RejectType();
            if constexpr (std::is_constructible_v<RejectType, std::string_view>) {
                reason = RejectType("Promise has been moved");
            }
            func(reason);
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
