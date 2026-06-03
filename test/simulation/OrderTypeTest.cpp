#include "catch2/catch_all.hpp"

#include "SimEvents.hpp"
#include "common/BasicTypes.hpp"
#include "common/MarketDataEvent.hpp"
#include "simulation/EngineView.hpp"
#include "simulation/HistoricalLOB.hpp"
#include "simulation/SimulatedLOB.hpp"

#include <vector>

using namespace cmf;
using namespace cmf::sim;

TEST_CASE("submit: a marketable order walks multiple book levels", "[sim][ordertype]")
{
    HistoricalLOB<> h;
    h.apply(add(1, Side::Sell, 101, 3));
    h.apply(add(2, Side::Sell, 102, 4));
    EngineView<> ev(h);
    SimulatedLOB<> s(ev);

    ev.submit(Side::Buy, 102, 5, TimeInForce::GoodTillCancel, 0);

    REQUIRE(ev.fills().size() == 2);
    CHECK(ev.fills()[0].price == 101);
    CHECK(ev.fills()[0].qty == 3);
    CHECK(ev.fills()[0].full == false);
    CHECK(ev.fills()[1].price == 102);
    CHECK(ev.fills()[1].qty == 2);
    CHECK(ev.fills()[1].full == true);
    CHECK(ev.position().net_qty == 5);
    CHECK(s.volume_at(Side::Sell, 101) == 0);
    CHECK(s.volume_at(Side::Sell, 102) == 2);
}

TEST_CASE("submit IOC fills what it can and does not rest the residual", "[sim][ordertype]")
{
    HistoricalLOB<> h;
    h.apply(add(1, Side::Sell, 101, 3));
    EngineView<> ev(h);

    ev.submit(Side::Buy, 101, 5, TimeInForce::FillAndKill, 0);

    CHECK(ev.fill_count() == 1);
    CHECK(ev.position().net_qty == 3);
    CHECK(ev.own_qty(Side::Buy, 101) == 0);
}

TEST_CASE("submit FOK rejects when it cannot fully fill, with no side effects", "[sim][ordertype]")
{
    HistoricalLOB<> h;
    h.apply(add(1, Side::Sell, 101, 3));
    EngineView<> ev(h);

    const auto handle = ev.submit(Side::Buy, 101, 5, TimeInForce::FillOrKill, 0);

    CHECK_FALSE(handle.ok());
    CHECK(ev.fill_count() == 0);
    CHECK(ev.position().net_qty == 0);
    CHECK(ev.consumed_qty(Side::Sell, 101) == 0);
}

TEST_CASE("submit FOK fills fully when it can", "[sim][ordertype]")
{
    HistoricalLOB<> h;
    h.apply(add(1, Side::Sell, 101, 5));
    EngineView<> ev(h);

    const auto handle = ev.submit(Side::Buy, 101, 5, TimeInForce::FillOrKill, 0);

    CHECK(handle.ok());
    CHECK(ev.fill_count() == 1);
    CHECK(ev.position().net_qty == 5);
}

TEST_CASE("submit FOK across multiple levels: fills at sum, rejects at sum+1", "[sim][ordertype]")
{
    HistoricalLOB<> h;
    h.apply(add(1, Side::Sell, 101, 3));
    h.apply(add(2, Side::Sell, 102, 4));

    EngineView<> a(h);
    CHECK(a.submit(Side::Buy, 102, 7, TimeInForce::FillOrKill, 0).ok());
    CHECK(a.position().net_qty == 7);
    CHECK(a.fill_count() == 2);

    EngineView<> b(h);
    CHECK_FALSE(b.submit(Side::Buy, 102, 8, TimeInForce::FillOrKill, 0).ok());
    CHECK(b.fill_count() == 0);
    CHECK(b.consumed_qty(Side::Sell, 101) == 0);
    CHECK(b.consumed_qty(Side::Sell, 102) == 0);
}

TEST_CASE("submit walk skips a consumed-out level and fills the next", "[sim][ordertype]")
{
    HistoricalLOB<> h;
    h.apply(add(1, Side::Sell, 101, 3));
    h.apply(add(2, Side::Sell, 102, 4));
    EngineView<> ev(h);

    ev.submit_limit(Side::Buy, 101, 3, 0);
    ev.clear_fills();

    ev.submit(Side::Buy, 102, 4, TimeInForce::GoodTillCancel, 0);

    REQUIRE(ev.fills().size() == 1);
    CHECK(ev.fills()[0].price == 102);
    CHECK(ev.fills()[0].qty == 4);
}

TEST_CASE("submit_market on an empty book fills nothing and rests nothing", "[sim][ordertype]")
{
    HistoricalLOB<> h;
    EngineView<> ev(h);

    const auto handle = ev.submit_market(Side::Buy, 5, 0);

    CHECK(handle.ok());
    CHECK(ev.fill_count() == 0);
    CHECK(ev.position().net_qty == 0);
    CHECK(ev.own_qty(Side::Buy, 101) == 0);
}

TEST_CASE("submit_market sweeps the book at any price and never rests", "[sim][ordertype]")
{
    HistoricalLOB<> h;
    h.apply(add(1, Side::Sell, 101, 3));
    h.apply(add(2, Side::Sell, 102, 4));
    EngineView<> ev(h);

    ev.submit_market(Side::Buy, 10, 0);

    CHECK(ev.position().net_qty == 7);
    CHECK(ev.own_qty(Side::Buy, 101) == 0);
    CHECK(ev.own_qty(Side::Buy, 102) == 0);
}

TEST_CASE("EngineView retains fills until clear_fills, fill_count is cumulative", "[sim][ordertype]")
{
    HistoricalLOB<> h;
    h.apply(add(1, Side::Sell, 101, 3));

    EngineView<> ev(h);
    ev.submit_limit(Side::Buy, 101, 3, 0);

    REQUIRE(ev.fills().size() == 1);
    CHECK(ev.fills()[0].qty == 3);
    CHECK(ev.fills()[0].price == 101);
    CHECK(ev.fill_count() == 1);

    ev.clear_fills();
    CHECK(ev.fills().empty());
    CHECK(ev.fill_count() == 1);
}

TEST_CASE("submit rejects invalid inputs with no side effects", "[sim][ordertype]")
{
    HistoricalLOB<> h;
    h.apply(add(1, Side::Sell, 101, 5));
    EngineView<> ev(h);

    CHECK_FALSE(ev.submit(Side::Buy, 101, 0, TimeInForce::GoodTillCancel, 0).ok());
    CHECK_FALSE(ev.submit(Side::Buy, 101, -3, TimeInForce::GoodTillCancel, 0).ok());
    CHECK_FALSE(ev.submit(Side::None, 101, 1, TimeInForce::GoodTillCancel, 0).ok());
    CHECK_FALSE(ev.submit(Side::Buy, PRICE_NONE, 1, TimeInForce::GoodTillCancel, 0).ok());
    CHECK_FALSE(ev.submit(Side::Buy, UNDEF_PRICE, 1, TimeInForce::GoodTillCancel, 0).ok());

    CHECK(ev.fill_count() == 0);
    CHECK(ev.position().net_qty == 0);
    CHECK(ev.own_qty(Side::Buy, 101) == 0);
    CHECK(ev.consumed_qty(Side::Sell, 101) == 0);
}

TEST_CASE("submit rejects invalid inputs before queueing under latency", "[sim][ordertype]")
{
    HistoricalLOB<> h;
    h.apply(add(1, Side::Sell, 101, 5));
    EngineView<> ev(h, 0, FeeSchedule{}, 10);

    CHECK_FALSE(ev.submit(Side::None, 101, 1, TimeInForce::GoodTillCancel, 0).ok());
    CHECK_FALSE(ev.submit(Side::Buy, 101, 0, TimeInForce::GoodTillCancel, 0).ok());

    ev.activate_due(1000);
    CHECK(ev.fill_count() == 0);
    CHECK(ev.position().net_qty == 0);
}

TEST_CASE("submit_market on the sell side sweeps the bid book", "[sim][ordertype]")
{
    HistoricalLOB<> h;
    h.apply(add(1, Side::Buy, 100, 4));
    h.apply(add(2, Side::Buy, 99, 5));
    EngineView<> ev(h);

    ev.submit_market(Side::Sell, 6, 0);

    CHECK(ev.position().net_qty == -6);
    REQUIRE(ev.fills().size() == 2);
    CHECK(ev.fills()[0].price == 100);
    CHECK(ev.fills()[0].side == Side::Sell);
    CHECK(ev.fills()[0].maker == false);
    CHECK(ev.fills()[1].price == 99);
    CHECK(ev.own_qty(Side::Sell, 100) == 0);
}

TEST_CASE("sell FillOrKill rejects when the bid side cannot fully fill", "[sim][ordertype]")
{
    HistoricalLOB<> h;
    h.apply(add(1, Side::Buy, 100, 5));
    EngineView<> ev(h);

    CHECK_FALSE(ev.submit(Side::Sell, 100, 7, TimeInForce::FillOrKill, 0).ok());
    CHECK(ev.fill_count() == 0);
    CHECK(ev.position().net_qty == 0);

    CHECK(ev.submit(Side::Sell, 100, 5, TimeInForce::FillOrKill, 0).ok());
    CHECK(ev.position().net_qty == -5);
}

TEST_CASE("sell FillAndKill fills the crossable depth and drops the residual", "[sim][ordertype]")
{
    HistoricalLOB<> h;
    h.apply(add(1, Side::Buy, 100, 3));
    EngineView<> ev(h);

    ev.submit(Side::Sell, 100, 5, TimeInForce::FillAndKill, 0);

    CHECK(ev.position().net_qty == -3);
    CHECK(ev.own_qty(Side::Sell, 100) == 0);
}
