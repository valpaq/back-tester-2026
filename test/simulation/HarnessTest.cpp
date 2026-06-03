#include "catch2/catch_all.hpp"

#include "SimEvents.hpp"
#include "common/MarketDataEvent.hpp"
#include "simulation/SimulationHarness.hpp"
#include "simulation/Strategy.hpp"

#include <cstdint>
#include <vector>

using namespace cmf;
using namespace cmf::sim;

static SimulationHarness<NaiveTaker> make_harness()
{
    std::vector<std::uint32_t> instruments{5, 7};
    std::vector<NaiveTaker> strategies;
    for (int e = 0; e < 4; ++e)
        strategies.push_back(NaiveTaker{1000000, static_cast<std::int64_t>(e + 1)});
    return SimulationHarness<NaiveTaker>(instruments, strategies, 2);
}

TEST_CASE("SimulationHarness runs N engines over multiple instruments", "[sim][harness]")
{
    auto harness = make_harness();
    harness(ev(5, 1, Action::Add, Side::Sell, 101, 1000));
    harness(ev(7, 2, Action::Add, Side::Sell, 201, 1000));
    harness.flush();

    CHECK(harness.engine_count() == 4);
    CHECK(harness.instrument_count() == 2);
    CHECK(harness.total_fills() == 8);

    REQUIRE_FALSE(harness.engine_view(0, 0).fills().empty());
    CHECK(harness.engine_view(0, 0).fills()[0].qty == 1);
    CHECK(harness.engine_view(0, 0).fills()[0].price == 101);
    CHECK(harness.engine_view(3, 0).fills()[0].qty == 4);

    CHECK(harness.engine_view(1, 1).fills()[0].price == 201);
}

TEST_CASE("SimulationHarness records taker positions and fees", "[sim][harness]")
{
    std::vector<std::uint32_t> instruments{5};
    std::vector<NaiveTaker> strategies(2, NaiveTaker{1000000, 1});
    SimulationHarness<NaiveTaker> harness(instruments, strategies, 1,
                                          FeeSchedule{10.0, 0.0});

    harness(ev(5, 1, Action::Add, Side::Sell, 101, 1000));
    harness.flush();

    const auto& pos = harness.engine_view(0, 0).position();
    CHECK(pos.net_qty == 1);
    CHECK(pos.fees > 0.0);
    CHECK(harness.engine_net_pnl(0) < 0.0);
    CHECK(harness.total_net_pnl() < 0.0);
}

TEST_CASE("SimulationHarness is deterministic across runs", "[sim][harness]")
{
    auto a = make_harness();
    a(ev(5, 1, Action::Add, Side::Sell, 101, 1000));
    a(ev(7, 2, Action::Add, Side::Sell, 201, 1000));
    a.flush();

    auto b = make_harness();
    b(ev(5, 1, Action::Add, Side::Sell, 101, 1000));
    b(ev(7, 2, Action::Add, Side::Sell, 201, 1000));
    b.flush();

    CHECK(a.total_fills() == b.total_fills());
    for (std::size_t e = 0; e < a.engine_count(); ++e)
        CHECK(a.engine_view(e, 0).fills()[0].qty == b.engine_view(e, 0).fills()[0].qty);
}

TEST_CASE("SimulationHarness normalizes chunk_size 0 to 1", "[sim][harness]")
{
    std::vector<std::uint32_t> instruments{5};
    std::vector<NaiveTaker> strategies(1, NaiveTaker{1000000, 1});
    SimulationHarness<NaiveTaker> harness(instruments, strategies, 0);

    harness(ev(5, 1, Action::Add, Side::Sell, 101, 10));

    CHECK(harness.total_fills() == 1);
}

TEST_CASE("SimulationHarness fills a PassiveMaker from a later chunk's trade", "[sim][harness]")
{
    std::vector<std::uint32_t> instruments{5};
    std::vector<PassiveMaker> strategies(1, PassiveMaker{100, 2});
    SimulationHarness<PassiveMaker> harness(instruments, strategies, 1);

    harness(ev(5, 1, Action::Add, Side::Sell, 200, 10));
    harness(ev(5, 0, Action::Trade, Side::Sell, 100, 2));
    harness.flush();

    CHECK(harness.total_fills() >= 1);
    CHECK(harness.engine_view(0, 0).position().net_qty == 2);
}

TEST_CASE("SimulationHarness is deterministic with multi-instrument latency clocks", "[sim][harness]")
{
    const std::vector<std::uint32_t> instruments{5, 7};
    const std::vector<NaiveTaker> strat(4, NaiveTaker{1000000, 1});

    auto feed = [](SimulationHarness<NaiveTaker>& hh)
    {
        hh(ev(5, 1, Action::Add, Side::Sell, 101, 100, 0));
        hh(ev(7, 2, Action::Add, Side::Sell, 201, 100, 5));
        hh(ev(5, 3, Action::Add, Side::Sell, 101, 100, 10));
        hh(ev(7, 4, Action::Add, Side::Sell, 201, 100, 12));
        hh(ev(5, 5, Action::Add, Side::Sell, 101, 100, 20));
        hh.flush();
    };

    SimulationHarness<NaiveTaker> a(instruments, strat, 1, FeeSchedule{}, 3);
    SimulationHarness<NaiveTaker> b(instruments, strat, 1, FeeSchedule{}, 3);
    feed(a);
    feed(b);

    CHECK(a.total_fills() == b.total_fills());
    CHECK(a.total_fills() > 0);
    for (std::size_t e = 0; e < 4; ++e)
        for (std::size_t j = 0; j < 2; ++j)
            CHECK(a.engine_view(e, j).position().net_qty == b.engine_view(e, j).position().net_qty);
}
