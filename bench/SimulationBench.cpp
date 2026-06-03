#include "common/MarketDataEvent.hpp"
#include "simulation/EngineView.hpp"
#include "simulation/HistoricalLOB.hpp"
#include "simulation/SimulatedLOB.hpp"
#include "simulation/SimulationHarness.hpp"
#include "simulation/Strategy.hpp"
#include "simulation/testing/EventBuilders.hpp"
#include <benchmark/benchmark.h>

#include <cstdint>
#include <vector>

using namespace cmf;
using namespace cmf::sim;

static void BM_HarnessThroughput(benchmark::State& state)
{
    const auto n_engines = static_cast<std::size_t>(state.range(0));
    constexpr uint32_t n_instruments = 4;
    constexpr std::size_t chunk = 16384;

    std::vector<std::uint32_t> instruments;
    for (uint32_t i = 0; i < n_instruments; ++i)
        instruments.push_back(i + 1);
    std::vector<NaiveTaker> strategies(n_engines, NaiveTaker{1000000, 1});

    SimulationHarness<NaiveTaker> harness(instruments, strategies, chunk);
    for (uint32_t i = 0; i < n_instruments; ++i)
        harness(ev(i + 1, i + 1, Action::Add, Side::Sell, 101, 1'000'000'000));

    std::vector<MarketDataEvent> batch;
    batch.reserve(chunk);
    for (std::size_t k = 0; k < chunk; ++k)
        batch.push_back(ev(static_cast<uint32_t>(k % n_instruments) + 1, 0, Action::Trade,
                           Side::Sell, 90, 1));

    std::uint64_t events = 0;
    for (auto _ : state)
    {
        for (const auto& e : batch)
            harness(e);
        events += batch.size();
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(events));
}
BENCHMARK(BM_HarnessThroughput)->Arg(1)->Arg(4)->Arg(16)->Arg(64);

static void BM_OverlayReadLatency(benchmark::State& state)
{
    HistoricalLOB<> h;
    for (int i = 0; i < 50; ++i)
    {
        h.apply(ev(1, static_cast<uint64_t>(i + 1), Action::Add, Side::Sell, 101 + i, 10));
        h.apply(ev(1, static_cast<uint64_t>(1000 + i), Action::Add, Side::Buy, 100 - i, 10));
    }
    EngineView<> v(h);
    v.submit_limit(Side::Buy, 101, 3, 0);
    SimulatedLOB<> sim(v);

    for (auto _ : state)
    {
        benchmark::DoNotOptimize(sim.best_price(Side::Sell));
        benchmark::DoNotOptimize(sim.best_price(Side::Buy));
        benchmark::DoNotOptimize(sim.volume_at(Side::Sell, 101));
    }
}
BENCHMARK(BM_OverlayReadLatency);
