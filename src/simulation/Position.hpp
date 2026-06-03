#pragma once

#include "common/BasicTypes.hpp"

#include <cmath>
#include <cstdint>
#include <cstdlib>

namespace cmf::sim
{

struct Position
{
    std::int64_t net_qty = 0;
    double avg_px = 0.0;
    double realized_pnl = 0.0;
    double fees = 0.0;

    void apply_fill(Side side, std::int64_t qty, double px)
    {
        if (qty <= 0 || (side != Side::Buy && side != Side::Sell))
            return;
        const std::int64_t dq = (side == Side::Buy) ? qty : -qty;

        if (net_qty == 0)
        {
            net_qty = dq;
            avg_px = px;
            return;
        }
        if ((net_qty > 0) == (dq > 0))
        {
            const double held = std::abs(static_cast<double>(net_qty));
            avg_px = (held * avg_px + static_cast<double>(qty) * px) /
                     (held + static_cast<double>(qty));
            net_qty += dq;
            return;
        }

        const std::int64_t closing =
            std::min<std::int64_t>(qty, std::llabs(net_qty));
        const double sign = (net_qty > 0) ? 1.0 : -1.0;
        realized_pnl += sign * (px - avg_px) * static_cast<double>(closing);

        const bool was_long = net_qty > 0;
        net_qty += dq;
        if (net_qty == 0)
            avg_px = 0.0;
        else if ((net_qty > 0) != was_long)
            avg_px = px;
    }

    [[nodiscard]] double net_pnl() const noexcept { return realized_pnl - fees; }

    [[nodiscard]] double unrealized_pnl(double mark_px) const noexcept
    {
        return net_qty == 0 ? 0.0 : (mark_px - avg_px) * static_cast<double>(net_qty);
    }
};

inline constexpr double kBpsScale = 1e-4;

struct FeeSchedule
{
    double taker_bps = 0.0;
    double maker_bps = 0.0;

    [[nodiscard]] double fee(double notional, bool maker) const noexcept
    {
        return notional * (maker ? maker_bps : taker_bps) * kBpsScale;
    }
};

}
