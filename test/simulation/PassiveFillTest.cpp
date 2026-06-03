#include "catch2/catch_all.hpp"

#include "SimEvents.hpp"
#include "common/MarketDataEvent.hpp"
#include "simulation/EngineView.hpp"
#include "simulation/HistoricalLOB.hpp"
#include "simulation/SimulatedLOB.hpp"

#include <vector>

using namespace cmf;
using namespace cmf::sim;

TEST_CASE("passive fill: a trade depletes the historical queue then fills the engine", "[sim][passive]")
{
    HistoricalLOB<> h;
    h.apply(add(1, Side::Buy, 100, 5));
    EngineView<> ev(h);
    ev.submit_limit(Side::Buy, 100, 3, 0);

    std::vector<Trade> trades{aggress(Side::Sell, 100, 7, 1)};
    ev.process_trades(trades);

    REQUIRE(ev.fills().size() == 1);
    CHECK(ev.fills()[0].qty == 2);
    CHECK(ev.fills()[0].side == Side::Buy);
    CHECK(ev.fills()[0].full == false);
    CHECK(ev.own_qty(Side::Buy, 100) == 1);
}

TEST_CASE("passive fill: no fill while trade volume stays behind the queue", "[sim][passive]")
{
    HistoricalLOB<> h;
    h.apply(add(1, Side::Buy, 100, 5));
    EngineView<> ev(h);
    ev.submit_limit(Side::Buy, 100, 3, 0);

    std::vector<Trade> first{aggress(Side::Sell, 100, 3, 1)};
    ev.process_trades(first);
    CHECK(ev.fills().empty());
    CHECK(ev.own_qty(Side::Buy, 100) == 3);

    std::vector<Trade> second{aggress(Side::Sell, 100, 4, 2)};
    ev.process_trades(second);
    REQUIRE(ev.fills().size() == 1);
    CHECK(ev.fills()[0].qty == 2);
    CHECK(ev.own_qty(Side::Buy, 100) == 1);
}

TEST_CASE("passive fill: q_ahead is snapshotted once and not raised by later historical adds", "[sim][passive]")
{
    HistoricalLOB<> h;
    h.apply(add(1, Side::Buy, 100, 5));
    EngineView<> ev(h);
    ev.submit_limit(Side::Buy, 100, 3, 0);
    h.apply(add(2, Side::Buy, 100, 10));

    std::vector<Trade> trades{aggress(Side::Sell, 100, 6, 1)};
    ev.process_trades(trades);

    REQUIRE(ev.fills().size() == 1);
    CHECK(ev.fills()[0].qty == 1);
}

TEST_CASE("reconcile_consumed clamps consumption to a shrunken basement level", "[sim][passive]")
{
    HistoricalLOB<> h;
    h.apply(add(2, Side::Sell, 101, 5));
    EngineView<> ev(h);
    SimulatedLOB<> s(ev);

    ev.submit_limit(Side::Buy, 101, 3, 0);
    h.apply(cancel(2, 2));
    ev.reconcile_consumed();
    h.apply(add(3, Side::Sell, 101, 3));

    CHECK(s.volume_at(Side::Sell, 101) == 3);
}

TEST_CASE("reconcile_consumed forces q_ahead to zero when the level is removed", "[sim][passive]")
{
    HistoricalLOB<> h;
    h.apply(add(1, Side::Buy, 100, 5));
    EngineView<> ev(h);
    ev.submit_limit(Side::Buy, 100, 3, 0);
    h.apply(cancel(1, 0));
    ev.reconcile_consumed();

    std::vector<Trade> trades{aggress(Side::Sell, 100, 2, 1)};
    ev.process_trades(trades);

    REQUIRE(ev.fills().size() == 1);
    CHECK(ev.fills()[0].qty == 2);
}

TEST_CASE("passive fill: own orders at one price fill FIFO by submission", "[sim][passive]")
{
    HistoricalLOB<> h;
    EngineView<> ev(h);
    const auto a = ev.submit_limit(Side::Buy, 100, 2, 0);
    const auto b = ev.submit_limit(Side::Buy, 100, 3, 0);

    std::vector<Trade> trades{aggress(Side::Sell, 100, 4, 1)};
    ev.process_trades(trades);

    REQUIRE(ev.fills().size() == 2);
    CHECK(ev.fills()[0].order_id == a.id);
    CHECK(ev.fills()[0].qty == 2);
    CHECK(ev.fills()[0].full);
    CHECK(ev.fills()[1].order_id == b.id);
    CHECK(ev.fills()[1].qty == 2);
    CHECK_FALSE(ev.fills()[1].full);
    CHECK(ev.own_qty(Side::Buy, 100) == 1);
}

TEST_CASE("passive fill: re-quoting at a swept price does not reset the queue", "[sim][passive]")
{
    HistoricalLOB<> h;
    h.apply(add(1, Side::Buy, 100, 5));
    EngineView<> ev(h);
    SimulatedLOB<> s(ev);

    ev.submit_limit(Side::Buy, 100, 2, 0);

    std::vector<Trade> sweep{aggress(Side::Sell, 100, 7, 1)};
    ev.process_trades(sweep);
    REQUIRE(ev.own_qty(Side::Buy, 100) == 0);
    CHECK(s.volume_at(Side::Buy, 100) == 0);

    ev.submit_limit(Side::Buy, 100, 3, 0);
    std::vector<Trade> more{aggress(Side::Sell, 100, 4, 2)};
    ev.process_trades(more);

    CHECK(ev.own_qty(Side::Buy, 100) == 0);
}

TEST_CASE("passive fill: a trade on the wrong side does not fill", "[sim][passive]")
{
    HistoricalLOB<> h;
    EngineView<> ev(h);
    ev.submit_limit(Side::Buy, 100, 4, 0);

    std::vector<Trade> trades{aggress(Side::Buy, 100, 10, 1)};
    ev.process_trades(trades);

    CHECK(ev.fills().empty());
    CHECK(ev.own_qty(Side::Buy, 100) == 4);
}

TEST_CASE("passive fill: Fill records are ignored by the passive pass", "[sim][passive]")
{
    HistoricalLOB<> h;
    EngineView<> ev(h);
    ev.submit_limit(Side::Buy, 100, 4, 0);

    std::vector<Trade> trades{fill_record(Side::Buy, 100, 10)};
    ev.process_trades(trades);

    CHECK(ev.fills().empty());
    CHECK(ev.own_qty(Side::Buy, 100) == 4);
}

TEST_CASE("EngineView applies maker rebate and tracks position on passive fills", "[sim][passive]")
{
    HistoricalLOB<> h;
    EngineView<> ev(h, 0, FeeSchedule{0.0, -5.0});
    ev.submit_limit(Side::Buy, 90, 10, 0);

    std::vector<Trade> trades{aggress(Side::Sell, 90, 10, 1)};
    ev.process_trades(trades);

    REQUIRE(ev.fills().size() == 1);
    CHECK(ev.fills()[0].maker == true);
    CHECK(ev.position().net_qty == 10);
    CHECK(ev.position().fees < 0.0);
    CHECK(ev.position().net_pnl() > 0.0);
}

TEST_CASE("EngineView retains passive fills and clear_fills resets the buffer", "[sim][passive]")
{
    HistoricalLOB<> h;
    EngineView<> ev(h);
    ev.submit_limit(Side::Buy, 90, 10, 0);

    std::vector<Trade> trades{aggress(Side::Sell, 90, 10, 1)};
    ev.process_trades(trades);

    REQUIRE(ev.fills().size() == 1);
    CHECK(ev.fills()[0].maker == true);
    CHECK(ev.fills()[0].qty == 10);
    CHECK(ev.fill_count() == 1);

    ev.clear_fills();
    CHECK(ev.fills().empty());
    CHECK(ev.fill_count() == 1);
}

TEST_CASE("reconcile_consumed leaves consumption that still fits the basement", "[sim][passive]")
{
    HistoricalLOB<> h;
    h.apply(add(2, Side::Sell, 101, 5));
    EngineView<> ev(h);
    SimulatedLOB<> s(ev);

    ev.submit_limit(Side::Buy, 101, 3, 0);
    h.apply(cancel(2, 4));
    ev.reconcile_consumed();

    CHECK(s.volume_at(Side::Sell, 101) == 1);
}

TEST_CASE("reconcile_consumed clamps q_ahead down when the basement depth shrinks", "[sim][passive]")
{
    HistoricalLOB<> h;
    h.apply(add(1, Side::Buy, 100, 10));
    EngineView<> ev(h);
    ev.submit_limit(Side::Buy, 100, 6, 0);

    h.apply(cancel(1, 4));
    ev.reconcile_consumed();

    std::vector<Trade> trades{aggress(Side::Sell, 100, 10, 1)};
    ev.process_trades(trades);

    REQUIRE(ev.fills().size() == 1);
    CHECK(ev.fills()[0].qty == 6);
    CHECK(ev.own_qty(Side::Buy, 100) == 0);
}

TEST_CASE("reconcile_consumed clamped q_ahead allows a partial passive fill", "[sim][passive]")
{
    HistoricalLOB<> h;
    h.apply(add(1, Side::Buy, 100, 10));
    EngineView<> ev(h);
    ev.submit_limit(Side::Buy, 100, 6, 0);

    h.apply(cancel(1, 4));
    ev.reconcile_consumed();

    std::vector<Trade> trades{aggress(Side::Sell, 100, 8, 1)};
    ev.process_trades(trades);

    REQUIRE(ev.fills().size() == 1);
    CHECK(ev.fills()[0].qty == 4);
    CHECK(ev.own_qty(Side::Buy, 100) == 2);
}

TEST_CASE("passive fill: the fill is stamped with the trade's ts_event", "[sim][passive]")
{
    HistoricalLOB<> h;
    EngineView<> ev(h);
    ev.submit_limit(Side::Buy, 90, 5, 0);

    std::vector<Trade> trades{Trade{90, 5, 777, 1, Side::Sell, Action::Trade}};
    ev.process_trades(trades);

    REQUIRE(ev.fills().size() == 1);
    CHECK(ev.fills()[0].ts == 777);
}

TEST_CASE("taker fill carries maker=false and pays a positive taker fee", "[sim][passive]")
{
    HistoricalLOB<> h;
    h.apply(add(1, Side::Sell, 100, 5));
    EngineView<> ev(h, 0, FeeSchedule{10.0, 0.0});

    ev.submit_limit(Side::Buy, 100, 5, 0);

    REQUIRE(ev.fills().size() == 1);
    CHECK(ev.fills()[0].maker == false);
    CHECK(ev.position().fees > 0.0);
}

TEST_CASE("passive fill: cancelling the middle order preserves FIFO of the survivors", "[sim][passive]")
{
    HistoricalLOB<> h;
    EngineView<> ev(h);
    const auto a = ev.submit_limit(Side::Buy, 99, 2, 0);
    const auto b = ev.submit_limit(Side::Buy, 99, 3, 0);
    const auto c = ev.submit_limit(Side::Buy, 99, 4, 0);

    REQUIRE(ev.cancel(b.id));

    std::vector<Trade> trades{aggress(Side::Sell, 99, 5, 1)};
    ev.process_trades(trades);

    REQUIRE(ev.fills().size() == 2);
    CHECK(ev.fills()[0].order_id == a.id);
    CHECK(ev.fills()[0].full);
    CHECK(ev.fills()[1].order_id == c.id);
    CHECK(ev.fills()[1].qty == 3);
    CHECK_FALSE(ev.fills()[1].full);
    CHECK(ev.own_qty(Side::Buy, 99) == 1);
}

TEST_CASE("passive fill: a partially filled order resumes FIFO across calls", "[sim][passive]")
{
    HistoricalLOB<> h;
    EngineView<> ev(h);
    const auto a = ev.submit_limit(Side::Buy, 100, 5, 0);
    const auto b = ev.submit_limit(Side::Buy, 100, 4, 0);

    ev.process_trades(std::vector<Trade>{aggress(Side::Sell, 100, 2, 1)});
    REQUIRE(ev.fills().size() == 1);
    CHECK(ev.fills()[0].order_id == a.id);
    CHECK_FALSE(ev.fills()[0].full);
    ev.clear_fills();

    ev.process_trades(std::vector<Trade>{aggress(Side::Sell, 100, 5, 2)});
    REQUIRE(ev.fills().size() == 2);
    CHECK(ev.fills()[0].order_id == a.id);
    CHECK(ev.fills()[0].qty == 3);
    CHECK(ev.fills()[0].full);
    CHECK(ev.fills()[1].order_id == b.id);
    CHECK(ev.fills()[1].qty == 2);
    CHECK(ev.own_qty(Side::Buy, 100) == 2);
}

TEST_CASE("passive fill: a recycled free-list slot keeps the new order identity", "[sim][passive]")
{
    HistoricalLOB<> h;
    EngineView<> ev(h);

    const auto a = ev.submit_limit(Side::Buy, 99, 2, 0);
    REQUIRE(ev.cancel(a.id));

    const auto b = ev.submit_limit(Side::Buy, 98, 3, 0);
    ev.process_trades(std::vector<Trade>{aggress(Side::Sell, 98, 3, 1)});

    REQUIRE(ev.fills().size() == 1);
    CHECK(ev.fills()[0].order_id == b.id);
    CHECK(ev.own_qty(Side::Buy, 98) == 0);

    ev.clear_fills();
    const auto d = ev.submit_limit(Side::Buy, 97, 1, 0);
    ev.process_trades(std::vector<Trade>{aggress(Side::Sell, 97, 1, 2)});
    REQUIRE(ev.fills().size() == 1);
    CHECK(ev.fills()[0].order_id == d.id);
}
