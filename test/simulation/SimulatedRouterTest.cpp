#include "catch2/catch_all.hpp"

#include "SimEvents.hpp"
#include "common/MarketDataEvent.hpp"
#include "simulation/SimulatedRouter.hpp"

#include <optional>

using namespace cmf;
using namespace cmf::sim;

TEST_CASE("SimulatedRouter keeps per-instrument basements isolated", "[sim][router]")
{
    SimulatedRouter<> r;
    r.apply(ev(5, 1, Action::Add, Side::Buy, 100, 5));
    r.apply(ev(7, 2, Action::Add, Side::Sell, 200, 9));

    CHECK(r.basement(5).best_price(Side::Buy) == std::optional<ScaledPrice>{100});
    CHECK(r.basement(7).best_price(Side::Sell) == std::optional<ScaledPrice>{200});
    CHECK(r.basement(5).empty(Side::Sell));
    CHECK(r.basement(7).empty(Side::Buy));
    CHECK(r.instrument_count() == 2);
}

TEST_CASE("SimulatedRouter resolves instrument from order id on follow-up events", "[sim][router]")
{
    SimulatedRouter<> r;
    r.apply(ev(5, 1, Action::Add, Side::Buy, 100, 5));
    r.apply(ev(0, 1, Action::Cancel, Side::None, 0, 0));

    CHECK(r.basement(5).empty(Side::Buy));
}

TEST_CASE("SimulatedRouter counts unresolved order ids and does not route them", "[sim][router]")
{
    SimulatedRouter<> r;
    r.apply(ev(0, 99, Action::Cancel, Side::None, 0, 0));

    CHECK(r.unresolved_count() == 1);
    CHECK(r.instrument_count() == 0);
}

TEST_CASE("SimulatedRouter routes instrument-tagged trades to the right basement", "[sim][router]")
{
    SimulatedRouter<> r;
    r.apply(ev(5, 1, Action::Add, Side::Buy, 100, 5));
    r.apply(ev(7, 0, Action::Trade, Side::Buy, 200, 3));

    CHECK(r.basement(7).chunk_trades().size() == 1);
    CHECK(r.basement(5).chunk_trades().empty());

    r.clear_all_chunk_trades();
    CHECK(r.basement(7).chunk_trades().empty());
}

TEST_CASE("SimulatedRouter basement references stay valid as instruments are added", "[sim][router]")
{
    SimulatedRouter<> r;
    r.apply(ev(5, 1, Action::Add, Side::Buy, 100, 5));
    const HistoricalLOB<>& five = r.basement(5);

    for (uint32_t i = 10; i < 200; ++i)
        r.apply(ev(i, i, Action::Add, Side::Buy, 100 + i, 1));

    CHECK(five.best_price(Side::Buy) == std::optional<ScaledPrice>{100});
}

TEST_CASE("SimulatedRouter find_basement is a read-only accessor", "[sim][router]")
{
    SimulatedRouter<> r;
    r.apply(ev(5, 1, Action::Add, Side::Buy, 100, 5));

    CHECK(r.find_basement(5) != nullptr);
    CHECK(r.find_basement(999) == nullptr);
    CHECK(r.instrument_count() == 1);
}

TEST_CASE("SimulatedRouter drops events with no instrument and no order", "[sim][router]")
{
    SimulatedRouter<> r;
    r.apply(ev(5, 1, Action::Add, Side::Buy, 100, 5));
    r.apply(ev(0, 0, Action::Clear, Side::None, 0, 0));

    CHECK(r.find_basement(0) == nullptr);
    CHECK(r.instrument_count() == 1);
}

TEST_CASE("SimulatedRouter resolves a trade's instrument from its order id", "[sim][router]")
{
    SimulatedRouter<> r;
    r.apply(ev(5, 1, Action::Add, Side::Buy, 100, 5));
    r.apply(ev(0, 1, Action::Trade, Side::Sell, 100, 3));

    CHECK(r.basement(5).chunk_trades().size() == 1);
}
