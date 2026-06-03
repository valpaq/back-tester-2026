#include "catch2/catch_all.hpp"

#include "SimEvents.hpp"
#include "common/BasicTypes.hpp"
#include "common/MarketDataEvent.hpp"
#include "simulation/EngineView.hpp"
#include "simulation/HistoricalLOB.hpp"
#include "simulation/SimulationHarness.hpp"
#include "simulation/Strategy.hpp"

#include <vector>

using namespace cmf;
using namespace cmf::sim;

TEST_CASE("latency: a marketable order does not fill until t + L", "[sim][latency]")
{
    HistoricalLOB<> h;
    h.apply(add(1, Side::Sell, 101, 5));
    EngineView<> ev(h, 0, FeeSchedule{}, 10);

    ev.submit(Side::Buy, 101, 5, TimeInForce::GoodTillCancel, 0);

    ev.activate_due(5);
    CHECK(ev.fill_count() == 0);
    CHECK(ev.position().net_qty == 0);

    ev.activate_due(10);
    CHECK(ev.fill_count() == 1);
    CHECK(ev.position().net_qty == 5);
}

TEST_CASE("latency: a pending order can be cancelled before it activates", "[sim][latency]")
{
    HistoricalLOB<> h;
    h.apply(add(1, Side::Sell, 101, 5));
    EngineView<> ev(h, 0, FeeSchedule{}, 10);

    const auto handle = ev.submit(Side::Buy, 101, 5, TimeInForce::GoodTillCancel, 0);
    CHECK(ev.cancel(handle.id));

    ev.activate_due(100);
    CHECK(ev.fill_count() == 0);
    CHECK(ev.position().net_qty == 0);
}

TEST_CASE("latency: FOK is re-checked against the book at activation, not submit", "[sim][latency]")
{
    HistoricalLOB<> h;
    h.apply(add(1, Side::Sell, 101, 5));
    EngineView<> ev(h, 0, FeeSchedule{}, 10);

    ev.submit(Side::Buy, 101, 5, TimeInForce::FillOrKill, 0);
    h.apply(cancel(1, 2));

    ev.activate_due(10);
    CHECK(ev.fill_count() == 0);
    CHECK(ev.position().net_qty == 0);
}

TEST_CASE("latency: a marketable order matches the book as of activation time", "[sim][latency]")
{
    HistoricalLOB<> h;
    h.apply(add(1, Side::Sell, 101, 5));
    EngineView<> ev(h, 0, FeeSchedule{}, 10);

    ev.submit(Side::Buy, 101, 5, TimeInForce::GoodTillCancel, 0);
    h.apply(cancel(1, 2));

    ev.activate_due(10);
    CHECK(ev.fill_count() == 1);
    CHECK(ev.position().net_qty == 2);
    CHECK(ev.own_qty(Side::Buy, 101) == 3);
}

TEST_CASE("latency: zero latency still fills immediately at submit", "[sim][latency]")
{
    HistoricalLOB<> h;
    h.apply(add(1, Side::Sell, 101, 5));
    EngineView<> ev(h);

    ev.submit(Side::Buy, 101, 5, TimeInForce::GoodTillCancel, 0);
    CHECK(ev.fill_count() == 1);
    CHECK(ev.position().net_qty == 5);
}

TEST_CASE("latency: an activated IOC drops its residual", "[sim][latency]")
{
    HistoricalLOB<> h;
    h.apply(add(1, Side::Sell, 101, 3));
    EngineView<> ev(h, 0, FeeSchedule{}, 10);

    ev.submit(Side::Buy, 101, 5, TimeInForce::FillAndKill, 0);
    ev.activate_due(10);

    CHECK(ev.position().net_qty == 3);
    CHECK(ev.own_qty(Side::Buy, 101) == 0);
}

TEST_CASE("latency: harness defers an order out of its submission chunk", "[sim][latency]")
{
    std::vector<std::uint32_t> instruments{5};
    std::vector<NaiveTaker> strategies(1, NaiveTaker{1000000, 1});
    SimulationHarness<NaiveTaker> harness(instruments, strategies, 1,
                                          FeeSchedule{}, 5);

    harness(ev(5, 1, Action::Add, Side::Sell, 101, 100, 0));
    CHECK(harness.total_fills() == 0);

    harness(ev(5, 2, Action::Add, Side::Sell, 101, 100, 5));
    CHECK(harness.total_fills() >= 1);
}

TEST_CASE("latency: activate_due fills only orders whose effective time has arrived", "[sim][latency]")
{
    HistoricalLOB<> h;
    h.apply(add(1, Side::Sell, 101, 2));
    EngineView<> ev(h, 0, FeeSchedule{}, 5);

    ev.submit(Side::Buy, 101, 1, TimeInForce::GoodTillCancel, 0);
    ev.submit(Side::Buy, 101, 1, TimeInForce::GoodTillCancel, 5);

    ev.activate_due(5);
    CHECK(ev.fill_count() == 1);

    ev.activate_due(10);
    CHECK(ev.fill_count() == 2);
}

TEST_CASE("latency: simultaneously-due orders activate in submission order", "[sim][latency]")
{
    HistoricalLOB<> h;
    h.apply(add(1, Side::Sell, 101, 2));
    EngineView<> ev(h, 0, FeeSchedule{}, 5);

    const auto a = ev.submit(Side::Buy, 101, 1, TimeInForce::GoodTillCancel, 0);
    const auto b = ev.submit(Side::Buy, 101, 1, TimeInForce::GoodTillCancel, 0);

    ev.activate_due(5);
    REQUIRE(ev.fills().size() == 2);
    CHECK(ev.fills()[0].order_id == a.id);
    CHECK(ev.fills()[1].order_id == b.id);
}

TEST_CASE("latency: a partial batch keeps the not-yet-due orders pending", "[sim][latency]")
{
    HistoricalLOB<> h;
    h.apply(add(1, Side::Sell, 101, 3));
    EngineView<> ev(h, 0, FeeSchedule{}, 5);

    ev.submit(Side::Buy, 101, 1, TimeInForce::GoodTillCancel, 0);
    ev.submit(Side::Buy, 101, 1, TimeInForce::GoodTillCancel, 3);
    ev.submit(Side::Buy, 101, 1, TimeInForce::GoodTillCancel, 6);

    ev.activate_due(7);
    CHECK(ev.fill_count() == 1);

    ev.activate_due(11);
    CHECK(ev.fill_count() == 3);
}

TEST_CASE("latency: an activated fill is stamped with the activation time", "[sim][latency]")
{
    HistoricalLOB<> h;
    h.apply(add(1, Side::Sell, 101, 5));
    EngineView<> ev(h, 0, FeeSchedule{}, 10);

    ev.submit(Side::Buy, 101, 5, TimeInForce::GoodTillCancel, 0);
    ev.activate_due(10);

    REQUIRE(ev.fills().size() == 1);
    CHECK(ev.fills()[0].ts == 10);
}

TEST_CASE("latency: the harness clock is per-instrument, not global", "[sim][latency]")
{
    std::vector<std::uint32_t> instruments{5, 7};
    std::vector<NaiveTaker> strategies(1, NaiveTaker{1000000, 1});
    SimulationHarness<NaiveTaker> harness(instruments, strategies, 1,
                                          FeeSchedule{}, 10);

    harness(ev(7, 1, Action::Add, Side::Sell, 201, 100, 0));
    harness(ev(5, 2, Action::Add, Side::Sell, 101, 100, 0));
    harness(ev(5, 3, Action::Add, Side::Sell, 101, 100, 50));
    harness(ev(5, 4, Action::Add, Side::Sell, 101, 100, 100));
    harness.flush();

    CHECK(harness.engine_view(0, 0).position().net_qty > 0);
    CHECK(harness.engine_view(0, 1).position().net_qty == 0);
}
