#include "catch2/catch_all.hpp"

#include "simulation/Position.hpp"

using namespace cmf;
using namespace cmf::sim;

using Catch::Approx;

TEST_CASE("Position: opening a long sets net and average", "[sim][pnl]")
{
    Position p;
    p.apply_fill(Side::Buy, 10, 100.0);
    CHECK(p.net_qty == 10);
    CHECK(p.avg_px == Approx(100.0));
    CHECK(p.realized_pnl == Approx(0.0));
}

TEST_CASE("Position: adding to a long updates the weighted average", "[sim][pnl]")
{
    Position p;
    p.apply_fill(Side::Buy, 10, 100.0);
    p.apply_fill(Side::Buy, 10, 110.0);
    CHECK(p.net_qty == 20);
    CHECK(p.avg_px == Approx(105.0));
    CHECK(p.realized_pnl == Approx(0.0));
}

TEST_CASE("Position: partially closing a long realizes PnL, keeps average", "[sim][pnl]")
{
    Position p;
    p.apply_fill(Side::Buy, 10, 100.0);
    p.apply_fill(Side::Sell, 4, 120.0);
    CHECK(p.net_qty == 6);
    CHECK(p.avg_px == Approx(100.0));
    CHECK(p.realized_pnl == Approx(80.0));
}

TEST_CASE("Position: closing a short realizes PnL", "[sim][pnl]")
{
    Position p;
    p.apply_fill(Side::Sell, 10, 100.0);
    p.apply_fill(Side::Buy, 10, 90.0);
    CHECK(p.net_qty == 0);
    CHECK(p.realized_pnl == Approx(100.0));
}

TEST_CASE("Position: flipping long to short realizes and re-bases", "[sim][pnl]")
{
    Position p;
    p.apply_fill(Side::Buy, 10, 100.0);
    p.apply_fill(Side::Sell, 15, 120.0);
    CHECK(p.net_qty == -5);
    CHECK(p.avg_px == Approx(120.0));
    CHECK(p.realized_pnl == Approx(200.0));
}

TEST_CASE("Position: flipping short to long realizes and re-bases", "[sim][pnl]")
{
    Position p;
    p.apply_fill(Side::Sell, 10, 100.0);
    p.apply_fill(Side::Buy, 15, 90.0);
    CHECK(p.net_qty == 5);
    CHECK(p.avg_px == Approx(90.0));
    CHECK(p.realized_pnl == Approx(100.0));
}

TEST_CASE("Position: an exact close goes flat, not flip", "[sim][pnl]")
{
    Position p;
    p.apply_fill(Side::Buy, 10, 100.0);
    p.apply_fill(Side::Sell, 10, 120.0);
    CHECK(p.net_qty == 0);
    CHECK(p.avg_px == Approx(0.0));
    CHECK(p.realized_pnl == Approx(200.0));
}

TEST_CASE("Position: short unrealized PnL has the right sign", "[sim][pnl]")
{
    Position p;
    p.apply_fill(Side::Sell, 10, 100.0);
    CHECK(p.unrealized_pnl(90.0) == Approx(100.0));
    CHECK(p.unrealized_pnl(110.0) == Approx(-100.0));
}

TEST_CASE("Position: unrealized PnL marks the open position", "[sim][pnl]")
{
    Position p;
    p.apply_fill(Side::Buy, 10, 100.0);
    CHECK(p.unrealized_pnl(105.0) == Approx(50.0));
    p.apply_fill(Side::Sell, 10, 100.0);
    CHECK(p.unrealized_pnl(105.0) == Approx(0.0));
}

TEST_CASE("FeeSchedule: taker pays, maker rebates, net_pnl nets fees", "[sim][pnl]")
{
    const FeeSchedule fees{2.0, -1.0};
    CHECK(fees.fee(1000.0, false) == Approx(0.2));
    CHECK(fees.fee(1000.0, true) == Approx(-0.1));

    Position p;
    p.apply_fill(Side::Buy, 10, 100.0);
    p.apply_fill(Side::Sell, 10, 110.0);
    p.fees += fees.fee(1000.0, false) + fees.fee(1100.0, false);
    CHECK(p.realized_pnl == Approx(100.0));
    CHECK(p.net_pnl() == Approx(100.0 - (0.2 + 0.22)));
}
