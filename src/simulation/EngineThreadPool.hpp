#pragma once

#include <algorithm>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace cmf::sim
{

class EngineThreadPool
{
  public:
    explicit EngineThreadPool(std::size_t n_engines)
    {
        unsigned hw = std::thread::hardware_concurrency();
        if (hw == 0)
            hw = 1;
        n_threads_ = n_engines == 0 ? 1 : std::min<std::size_t>(hw, n_engines);
        for (std::size_t t = 1; t < n_threads_; ++t)
            workers_.emplace_back([this, t]
                                  { worker_loop(t); });
    }

    ~EngineThreadPool()
    {
        {
            const std::lock_guard lk(m_);
            stop_ = true;
            ++generation_;
        }
        cv_work_.notify_all();
    }

    EngineThreadPool(const EngineThreadPool&) = delete;
    EngineThreadPool& operator=(const EngineThreadPool&) = delete;
    EngineThreadPool(EngineThreadPool&&) = delete;
    EngineThreadPool& operator=(EngineThreadPool&&) = delete;

    void run(std::size_t n_engines, const std::function<void(std::size_t)>& fn)
    {
        if (n_threads_ <= 1)
        {
            for (std::size_t e = 0; e < n_engines; ++e)
                fn(e);
            return;
        }

        {
            const std::lock_guard lk(m_);
            fn_ = &fn;
            n_engines_ = n_engines;
            remaining_ = n_threads_ - 1;
            error_ = nullptr;
            ++generation_;
        }
        cv_work_.notify_all();

        run_slice(0);

        std::exception_ptr err;
        {
            std::unique_lock lk(m_);
            cv_done_.wait(lk, [this]
                          { return remaining_ == 0; });
            fn_ = nullptr;
            err = error_;
        }

        if (err)
            std::rethrow_exception(err);
    }

  private:
    void run_slice(std::size_t tid)
    {
        try
        {
            for (std::size_t e = tid; e < n_engines_; e += n_threads_)
                (*fn_)(e);
        }
        catch (...)
        {
            const std::lock_guard lk(m_);
            if (!error_)
                error_ = std::current_exception();
        }
    }

    void worker_loop(std::size_t tid)
    {
        std::uint64_t seen = 0;
        while (true)
        {
            {
                std::unique_lock lk(m_);
                cv_work_.wait(lk, [this, &seen]
                              { return generation_ != seen || stop_; });
                if (stop_)
                    return;
                seen = generation_;
            }
            run_slice(tid);
            {
                const std::lock_guard lk(m_);
                if (--remaining_ == 0)
                    cv_done_.notify_one();
            }
        }
    }

    std::size_t n_threads_ = 1;
    std::mutex m_;
    std::condition_variable cv_work_;
    std::condition_variable cv_done_;
    const std::function<void(std::size_t)>* fn_ = nullptr;
    std::size_t n_engines_ = 0;
    std::uint64_t generation_ = 0;
    std::size_t remaining_ = 0;
    bool stop_ = false;
    std::exception_ptr error_;
    std::vector<std::jthread> workers_;
};

}
