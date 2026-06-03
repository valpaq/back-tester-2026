#include "catch2/catch_all.hpp"

#include "SimEvents.hpp"
#include "common/MarketDataEvent.hpp"
#include "simulation/HistoricalLOB.hpp"

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

using namespace cmf;
using namespace cmf::sim;

TEST_CASE("HistoricalLOB reconstructs the book like the inner LimitOrderBook", "[sim][hist]")
{
    HistoricalLOB<> h;
    h.apply(add(1, Side::Buy, 100, 5));
    h.apply(add(2, Side::Sell, 101, 7));

    CHECK(h.best_price(Side::Buy) == std::optional<ScaledPrice>{100});
    CHECK(h.best_price(Side::Sell) == std::optional<ScaledPrice>{101});
    CHECK(h.volume_at(Side::Buy, 100) == 5);
    CHECK(h.volume_at(Side::Sell, 101) == 7);
}

TEST_CASE("HistoricalLOB captures Trade/Fill without mutating the book", "[sim][hist]")
{
    HistoricalLOB<> h;
    h.apply(add(1, Side::Sell, 101, 7));
    h.apply(trade(Side::Sell, 101, 3, 42));

    CHECK(h.volume_at(Side::Sell, 101) == 7);
    REQUIRE(h.chunk_trades().size() == 1);
    const auto& t = h.chunk_trades()[0];
    CHECK(t.price == 101);
    CHECK(t.qty == 3);
    CHECK(t.side == Side::Sell);
    CHECK(t.sequence == 42);
    CHECK(t.action == Action::Trade);
}

TEST_CASE("HistoricalLOB clear_chunk_trades empties the log", "[sim][hist]")
{
    HistoricalLOB<> h;
    h.apply(trade(Side::Buy, 100, 1, 1));
    REQUIRE(h.chunk_trades().size() == 1);
    h.clear_chunk_trades();
    CHECK(h.chunk_trades().empty());
}

TEST_CASE("HistoricalLOB applies the bad-event skip filter to taps and book", "[sim][hist]")
{
    HistoricalLOB<> h;

    MarketDataEvent bad_add = add(1, Side::Buy, 100, 5);
    bad_add.flags = Flags::MaybeBadBook;
    h.apply(bad_add);

    MarketDataEvent bad_trade = trade(Side::Buy, 100, 1, 1);
    bad_trade.flags = Flags::BadTsRecv;
    h.apply(bad_trade);

    CHECK(h.empty(Side::Buy));
    CHECK(h.chunk_trades().empty());
}

TEST_CASE("HistoricalLOB version bumps only when the touched side's top changes", "[sim][hist]")
{
    HistoricalLOB<> h;

    const auto v_bid0 = h.version(Side::Buy);
    const auto v_ask0 = h.version(Side::Sell);

    h.apply(add(1, Side::Buy, 100, 5));
    CHECK(h.version(Side::Buy) > v_bid0);
    CHECK(h.version(Side::Sell) == v_ask0);

    const auto v_bid1 = h.version(Side::Buy);
    h.apply(add(2, Side::Buy, 90, 5));
    CHECK(h.version(Side::Buy) == v_bid1);

    h.apply(add(3, Side::Buy, 100, 4));
    CHECK(h.version(Side::Buy) > v_bid1);

    const auto v_bid2 = h.version(Side::Buy);
    h.apply(cancel(3, 0));
    CHECK(h.version(Side::Buy) > v_bid2);
}

TEST_CASE("HistoricalLOB version tracks empty-side transitions", "[sim][hist]")
{
    HistoricalLOB<> h;
    h.apply(add(1, Side::Buy, 100, 5));
    const auto v0 = h.version(Side::Buy);

    h.apply(cancel(1, 0));
    CHECK(h.empty(Side::Buy));
    CHECK(h.version(Side::Buy) > v0);

    const auto v1 = h.version(Side::Buy);
    h.apply(cancel(999, 0));
    CHECK(h.version(Side::Buy) == v1);
}

TEST_CASE("HistoricalLOB version reacts to Modify moving an order off the best", "[sim][hist]")
{
    HistoricalLOB<> h;
    h.apply(add(1, Side::Buy, 100, 5));
    h.apply(add(2, Side::Buy, 90, 5));
    const auto v0 = h.version(Side::Buy);

    h.apply(modify(2, Side::Buy, 80, 5));
    CHECK(h.version(Side::Buy) == v0);

    h.apply(modify(1, Side::Buy, 80, 5));
    CHECK(h.version(Side::Buy) > v0);
}

TEST_CASE("HistoricalLOB captures Fill records and leaves the book intact", "[sim][hist]")
{
    HistoricalLOB<> h;
    h.apply(add(1, Side::Buy, 100, 5));
    h.apply(fill(Side::Buy, 100, 2, 7));

    CHECK(h.volume_at(Side::Buy, 100) == 5);
    REQUIRE(h.chunk_trades().size() == 1);
    CHECK(h.chunk_trades()[0].action == Action::Fill);
    CHECK(h.chunk_trades()[0].side == Side::Buy);
}

TEST_CASE("HistoricalLOB for_each_level walks levels best-first", "[sim][hist]")
{
    HistoricalLOB<> h;
    h.apply(add(1, Side::Buy, 100, 5));
    h.apply(add(2, Side::Buy, 99, 3));
    h.apply(add(3, Side::Buy, 98, 1));

    std::vector<std::pair<ScaledPrice, std::int64_t>> levels;
    h.for_each_level(Side::Buy, [&](ScaledPrice p, std::int64_t q)
                     { levels.emplace_back(p, q); });

    REQUIRE(levels.size() == 3);
    CHECK(levels[0] == std::pair<ScaledPrice, std::int64_t>{100, 5});
    CHECK(levels[1] == std::pair<ScaledPrice, std::int64_t>{99, 3});
    CHECK(levels[2] == std::pair<ScaledPrice, std::int64_t>{98, 1});
}

TEST_CASE("HistoricalLOB keeps aggregated/synthetic trades out of the fill stream", "[sim][hist]")
{
    for (const Flags flag : {Flags::Tob, Flags::Mbp, Flags::Snapshot})
    {
        HistoricalLOB<> h;
        auto e = ev(0, 0, Action::Trade, Side::Sell, 100, 5);
        e.flags = flag;
        h.apply(e);
        CHECK(h.chunk_trades().empty());
    }

    HistoricalLOB<> h;
    h.apply(ev(0, 0, Action::Trade, Side::Sell, 100, 5));
    CHECK(h.chunk_trades().size() == 1);
}
