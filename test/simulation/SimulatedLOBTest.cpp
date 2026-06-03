#include "catch2/catch_all.hpp"

#include "SimEvents.hpp"
#include "common/MarketDataEvent.hpp"
#include "simulation/EngineView.hpp"
#include "simulation/HistoricalLOB.hpp"
#include "simulation/SimulatedLOB.hpp"

#include <optional>
#include <utility>
#include <vector>

using namespace cmf;
using namespace cmf::sim;
using Catch::Approx;

TEST_CASE("SimulatedLOB with an empty overlay equals the basement", "[sim][view]")
{
    HistoricalLOB<> h;
    h.apply(add(1, Side::Buy, 100, 5));
    h.apply(add(2, Side::Sell, 101, 7));
    EngineView<> ev(h);
    SimulatedLOB<> s(ev);

    CHECK(s.best_price(Side::Buy) == std::optional<ScaledPrice>{100});
    CHECK(s.best_price(Side::Sell) == std::optional<ScaledPrice>{101});
    CHECK(s.volume_at(Side::Buy, 100) == 5);
    CHECK(s.volume_at(Side::Sell, 101) == 7);
    CHECK(s.empty(Side::Buy) == false);
}

TEST_CASE("EngineView: a non-marketable order rests in the overlay", "[sim][view]")
{
    HistoricalLOB<> h;
    h.apply(add(1, Side::Buy, 100, 5));
    h.apply(add(2, Side::Sell, 101, 7));
    EngineView<> ev(h);
    SimulatedLOB<> s(ev);

    const auto handle = ev.submit_limit(Side::Buy, 99, 4, 0);

    CHECK(handle.ok());
    CHECK(ev.fills().empty());
    CHECK(s.volume_at(Side::Buy, 99) == 4);
    CHECK(s.best_price(Side::Buy) == std::optional<ScaledPrice>{100});
}

TEST_CASE("EngineView: a marketable order fills at touch and consumes opposite depth", "[sim][view]")
{
    HistoricalLOB<> h;
    h.apply(add(1, Side::Sell, 101, 5));
    h.apply(add(2, Side::Sell, 102, 5));
    EngineView<> ev(h);
    SimulatedLOB<> s(ev);

    const auto handle = ev.submit_limit(Side::Buy, 101, 3, 0);

    CHECK(handle.ok());
    REQUIRE(ev.fills().size() == 1);
    CHECK(ev.fills()[0].price == 101);
    CHECK(ev.fills()[0].qty == 3);
    CHECK(ev.fills()[0].side == Side::Buy);
    CHECK(s.volume_at(Side::Sell, 101) == 2);
    CHECK(s.best_price(Side::Sell) == std::optional<ScaledPrice>{101});
}

TEST_CASE("EngineView: fully consuming the touch advances the view's best ask", "[sim][view]")
{
    HistoricalLOB<> h;
    h.apply(add(1, Side::Sell, 101, 5));
    h.apply(add(2, Side::Sell, 102, 5));
    EngineView<> ev(h);
    SimulatedLOB<> s(ev);

    ev.submit_limit(Side::Buy, 101, 5, 0);

    CHECK(s.volume_at(Side::Sell, 101) == 0);
    CHECK(s.best_price(Side::Sell) == std::optional<ScaledPrice>{102});
}

TEST_CASE("EngineView: engines are isolated from one another", "[sim][view]")
{
    HistoricalLOB<> h;
    h.apply(add(1, Side::Sell, 101, 5));
    EngineView<> a(h);
    EngineView<> b(h);
    SimulatedLOB<> sa(a);
    SimulatedLOB<> sb(b);

    a.submit_limit(Side::Buy, 101, 5, 0);

    CHECK(sa.volume_at(Side::Sell, 101) == 0);
    CHECK(sb.volume_at(Side::Sell, 101) == 5);
    CHECK(sb.best_price(Side::Sell) == std::optional<ScaledPrice>{101});
    CHECK(b.fills().empty());
}

TEST_CASE("EngineView: cancel removes the own resting order", "[sim][view]")
{
    HistoricalLOB<> h;
    h.apply(add(1, Side::Buy, 100, 5));
    EngineView<> ev(h);
    SimulatedLOB<> s(ev);

    const auto handle = ev.submit_limit(Side::Buy, 99, 4, 0);
    REQUIRE(s.volume_at(Side::Buy, 99) == 4);

    CHECK(ev.cancel(handle.id));
    CHECK(s.volume_at(Side::Buy, 99) == 0);
    CHECK_FALSE(ev.cancel(handle.id));
}

TEST_CASE("SimulatedLOB side_levels merges own and netted historical levels", "[sim][view]")
{
    HistoricalLOB<> h;
    h.apply(add(1, Side::Buy, 100, 5));
    h.apply(add(2, Side::Buy, 98, 3));
    EngineView<> ev(h);
    SimulatedLOB<> s(ev);

    ev.submit_limit(Side::Buy, 99, 2, 0);

    const auto levels = s.side_levels(Side::Buy);
    std::vector<std::pair<ScaledPrice, ScaledPrice>> lv(levels.begin(), levels.end());

    REQUIRE(lv.size() == 3);
    CHECK(lv[0] == std::pair<ScaledPrice, ScaledPrice>{100, 5});
    CHECK(lv[1] == std::pair<ScaledPrice, ScaledPrice>{99, 2});
    CHECK(lv[2] == std::pair<ScaledPrice, ScaledPrice>{98, 3});
}

TEST_CASE("EngineView: partial fill then cancel removes only the residual", "[sim][view]")
{
    HistoricalLOB<> h;
    h.apply(add(1, Side::Sell, 101, 2));
    EngineView<> ev(h);
    SimulatedLOB<> s(ev);

    const auto handle = ev.submit_limit(Side::Buy, 101, 5, 0);
    REQUIRE(ev.fills().size() == 1);
    CHECK(ev.fills()[0].qty == 2);
    CHECK(ev.fills()[0].full == false);
    CHECK(s.volume_at(Side::Buy, 101) == 3);
    CHECK(s.volume_at(Side::Sell, 101) == 0);

    CHECK(ev.cancel(handle.id));
    CHECK(s.volume_at(Side::Buy, 101) == 0);
    CHECK(s.volume_at(Side::Sell, 101) == 0);
}

TEST_CASE("EngineView: cancel disaggregates one of several orders at a price", "[sim][view]")
{
    HistoricalLOB<> h;
    EngineView<> ev(h);
    SimulatedLOB<> s(ev);

    const auto first = ev.submit_limit(Side::Buy, 99, 4, 0);
    ev.submit_limit(Side::Buy, 99, 6, 0);
    REQUIRE(s.volume_at(Side::Buy, 99) == 10);

    CHECK(ev.cancel(first.id));
    CHECK(s.volume_at(Side::Buy, 99) == 6);
}

TEST_CASE("EngineView: the simplest model does not self-match", "[sim][view]")
{
    HistoricalLOB<> h;
    EngineView<> ev(h);
    SimulatedLOB<> s(ev);

    ev.submit_limit(Side::Buy, 105, 5, 0);
    const auto handle = ev.submit_limit(Side::Sell, 100, 3, 0);

    CHECK(handle.ok());
    CHECK(ev.fills().empty());
    CHECK(s.volume_at(Side::Buy, 105) == 5);
    CHECK(s.volume_at(Side::Sell, 100) == 3);
}

TEST_CASE("EngineView round-trip realizes PnL through the marketable path", "[sim][view][pnl]")
{
    HistoricalLOB<> h;
    h.apply(add(1, Side::Sell, 1'000'000'000, 10));
    h.apply(add(2, Side::Buy, 1'100'000'000, 10));
    EngineView<> ev(h);

    ev.submit_limit(Side::Buy, 1'000'000'000, 10, 0);
    CHECK(ev.position().net_qty == 10);
    ev.submit_limit(Side::Sell, 1'100'000'000, 10, 0);

    CHECK(ev.position().net_qty == 0);
    CHECK(ev.position().realized_pnl == Approx(1.0));
}

TEST_CASE("SimulatedLOB cached best price reflects later basement updates", "[sim][view]")
{
    HistoricalLOB<> h;
    h.apply(add(1, Side::Sell, 101, 5));
    EngineView<> ev(h);
    SimulatedLOB<> s(ev);

    CHECK(s.best_price(Side::Sell) == std::optional<ScaledPrice>{101});
    CHECK(s.best_price(Side::Sell) == std::optional<ScaledPrice>{101});

    h.apply(add(2, Side::Sell, 100, 5));

    CHECK(s.best_price(Side::Sell) == std::optional<ScaledPrice>{100});
}

TEST_CASE("SimulatedLOB cached best price reflects own order improvements", "[sim][view]")
{
    HistoricalLOB<> h;
    h.apply(add(1, Side::Buy, 100, 5));
    EngineView<> ev(h);
    SimulatedLOB<> s(ev);

    CHECK(s.best_price(Side::Buy) == std::optional<ScaledPrice>{100});
    ev.submit_limit(Side::Buy, 102, 3, 0);
    CHECK(s.best_price(Side::Buy) == std::optional<ScaledPrice>{102});
}

TEST_CASE("SimulatedLOB side_levels drops fully consumed historical levels", "[sim][view]")
{
    HistoricalLOB<> h;
    h.apply(add(1, Side::Sell, 101, 5));
    h.apply(add(2, Side::Sell, 102, 2));
    EngineView<> ev(h);
    SimulatedLOB<> s(ev);

    ev.submit_limit(Side::Buy, 101, 5, 0);

    const auto levels = s.side_levels(Side::Sell);
    std::vector<std::pair<ScaledPrice, ScaledPrice>> lv(levels.begin(), levels.end());

    REQUIRE(lv.size() == 1);
    CHECK(lv[0] == std::pair<ScaledPrice, ScaledPrice>{102, 2});
}

TEST_CASE("SimulatedLOB best ask reflects the lowest own sell level", "[sim][view]")
{
    HistoricalLOB<> h;
    EngineView<> ev(h);
    ev.submit_limit(Side::Sell, 100, 1, 0);
    ev.submit_limit(Side::Sell, 99, 1, 0);
    SimulatedLOB<> s(ev);

    CHECK(s.best_price(Side::Sell) == std::optional<ScaledPrice>{99});
}

TEST_CASE("SimulatedLOB side_levels sums own and historical depth at one price", "[sim][view]")
{
    HistoricalLOB<> h;
    h.apply(add(1, Side::Sell, 101, 5));
    h.apply(add(2, Side::Sell, 102, 2));
    EngineView<> ev(h);
    ev.submit_limit(Side::Sell, 101, 3, 0);
    SimulatedLOB<> s(ev);

    const auto levels = s.side_levels(Side::Sell);
    std::vector<std::pair<ScaledPrice, ScaledPrice>> lv(levels.begin(), levels.end());

    REQUIRE(lv.size() == 2);
    CHECK(lv[0] == std::pair<ScaledPrice, ScaledPrice>{101, 8});
    CHECK(lv[1] == std::pair<ScaledPrice, ScaledPrice>{102, 2});
}

TEST_CASE("EngineView taker flip realizes PnL and charges fees on both legs", "[sim][view][pnl]")
{
    HistoricalLOB<> h;
    h.apply(add(1, Side::Sell, 1'000'000'000, 10));
    h.apply(add(2, Side::Buy, 950'000'000, 20));
    EngineView<> ev(h, 0, FeeSchedule{10.0, 0.0});

    ev.submit_market(Side::Buy, 10, 0);
    ev.submit_market(Side::Sell, 15, 0);

    CHECK(ev.position().net_qty == -5);
    CHECK(ev.position().realized_pnl == Approx(-0.5));
    CHECK(ev.position().fees == Approx(0.02425));
    CHECK(ev.position().net_pnl() == Approx(-0.52425));
}
