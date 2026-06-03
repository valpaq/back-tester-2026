#include "catch2/catch_all.hpp"

#include "SimEvents.hpp"
#include "common/MarketDataEvent.hpp"
#include "simulation/EngineThreadPool.hpp"
#include "simulation/EngineView.hpp"
#include "simulation/HistoricalLOB.hpp"
#include "simulation/SimulatedLOB.hpp"

#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <stdexcept>
#include <tuple>
#include <vector>

using namespace cmf;
using namespace cmf::sim;

namespace
{
using FillKey = std::tuple<ClientOrderId, ScaledPrice, std::int64_t, int, bool>;

std::vector<std::vector<FillKey>> run_scenario(std::size_t n, std::size_t chunks, bool parallel,
                                               NanoTime latency = 0)
{
    HistoricalLOB<> h;
    std::deque<EngineView<>> engines;
    for (std::size_t e = 0; e < n; ++e)
        engines.emplace_back(h, static_cast<EngineId>(e), FeeSchedule{}, latency);

    std::size_t cur = 0;
    auto phase_a = [&](std::size_t c)
    {
        cur = c;
        h.clear_chunk_trades();
        if (c == 0)
        {
            h.apply(add(1, Side::Sell, 101, 1000));
            h.apply(add(2, Side::Buy, 90, 50));
        }
        else
        {
            h.apply(trade(Side::Sell, 90, 1000, static_cast<uint32_t>(c)));
        }
    };
    auto step = [&](std::size_t e)
    {
        EngineView<>& v = engines[e];
        v.reconcile_consumed();
        v.process_trades(h.chunk_trades());
        v.activate_due(static_cast<NanoTime>(cur));
        SimulatedLOB<> sim(v);
        const auto q = static_cast<std::int64_t>(e % 4 + 1);
        if (const auto ask = sim.best_price(Side::Sell))
            v.submit_limit(Side::Buy, *ask, q, static_cast<NanoTime>(cur));
        v.submit_limit(Side::Buy, 90, 3, static_cast<NanoTime>(cur));
    };

    if (parallel)
    {
        EngineThreadPool pool(n);
        const std::function<void(std::size_t)> fn = [&](std::size_t e)
        { step(e); };
        for (std::size_t c = 0; c < chunks; ++c)
        {
            phase_a(c);
            pool.run(n, fn);
        }
    }
    else
        for (std::size_t c = 0; c < chunks; ++c)
        {
            phase_a(c);
            for (std::size_t e = 0; e < n; ++e)
                step(e);
        }

    std::vector<std::vector<FillKey>> out(n);
    for (std::size_t e = 0; e < n; ++e)
        for (const Fill& f : engines[e].fills())
            out[e].push_back({f.order_id, f.price, f.qty, static_cast<int>(f.side), f.full});
    return out;
}
}

TEST_CASE("epoch-phased run is deterministic: parallel equals sequential", "[sim][concurrency]")
{
    const std::size_t n = 16;
    const std::size_t chunks = 8;

    const auto parallel = run_scenario(n, chunks, true);
    const auto sequential = run_scenario(n, chunks, false);

    CHECK(parallel == sequential);

    bool any = false;
    for (const auto& e : sequential)
        any = any || !e.empty();
    CHECK(any);
}

TEST_CASE("epoch-phased run keeps engines isolated (distinct per-engine fills)", "[sim][concurrency]")
{
    const auto result = run_scenario(8, 6, true);
    CHECK(result[0] != result[3]);
}

TEST_CASE("epoch-phased run with latency is deterministic: parallel equals sequential",
          "[sim][concurrency]")
{
    const std::size_t n = 16;
    const std::size_t chunks = 8;

    const auto parallel = run_scenario(n, chunks, true, 2);
    const auto sequential = run_scenario(n, chunks, false, 2);

    CHECK(parallel == sequential);

    bool any = false;
    for (const auto& e : sequential)
        any = any || !e.empty();
    CHECK(any);
}

TEST_CASE("EngineThreadPool propagates a worker exception", "[sim][concurrency]")
{
    EngineThreadPool pool(4);
    const std::function<void(std::size_t)> fn = [](std::size_t)
    { throw std::runtime_error("boom"); };
    CHECK_THROWS_AS(pool.run(4, fn), std::runtime_error);
}

TEST_CASE("EngineThreadPool with a single thread runs every engine inline", "[sim][concurrency]")
{
    EngineThreadPool pool(1);
    std::vector<std::atomic<int>> visits(3);
    const std::function<void(std::size_t)> fn = [&](std::size_t e)
    { visits[e].fetch_add(1, std::memory_order_relaxed); };

    pool.run(3, fn);

    for (std::size_t e = 0; e < 3; ++e)
        CHECK(visits[e].load() == 1);
}

TEST_CASE("EngineThreadPool visits each engine exactly once per run", "[sim][concurrency]")
{
    const std::size_t n = 50;
    EngineThreadPool pool(n);
    std::vector<std::atomic<int>> visits(n);
    const std::function<void(std::size_t)> fn = [&](std::size_t e)
    {
        visits[e].fetch_add(1, std::memory_order_relaxed);
    };

    for (int round = 0; round < 5; ++round)
        pool.run(n, fn);

    for (std::size_t e = 0; e < n; ++e)
        CHECK(visits[e].load() == 5);
}
