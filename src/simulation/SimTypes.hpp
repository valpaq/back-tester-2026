#pragma once

#include "common/BasicTypes.hpp"

#include <cstdint>
#include <limits>

namespace cmf::sim
{

using ClientOrderId = std::uint64_t;
using EngineId = std::uint16_t;

inline constexpr ScaledPrice PRICE_NONE = std::numeric_limits<ScaledPrice>::min();

inline constexpr double kPriceScale = 1e-9;

[[nodiscard]] inline constexpr double to_price(ScaledPrice p) noexcept
{
    return static_cast<double>(p) * kPriceScale;
}

[[nodiscard]] inline constexpr int side_idx(Side side) noexcept
{
    return side == Side::Buy ? 0 : 1;
}

[[nodiscard]] inline constexpr Side opposite(Side side) noexcept
{
    return side == Side::Buy ? Side::Sell : (side == Side::Sell ? Side::Buy : Side::None);
}

[[nodiscard]] inline constexpr bool price_is_better(Side side, ScaledPrice a, ScaledPrice b) noexcept
{
    return side == Side::Buy ? a > b : a < b;
}

[[nodiscard]] inline constexpr bool crosses(Side side, ScaledPrice limit, ScaledPrice level) noexcept
{
    return side == Side::Buy ? limit >= level : limit <= level;
}

enum class OrderState : std::uint8_t
{
    New,
    Resting,
    PartiallyFilled,
    Filled,
    Cancelled,
};

struct OrderHandle
{
    ClientOrderId id = 0;
    [[nodiscard]] bool ok() const noexcept { return id != 0; }
};

struct EngineOrder
{
    ScaledPrice price = PRICE_NONE;
    std::int64_t quantity = 0;
    ClientOrderId order_id = 0;
    std::uint32_t next_free = 0xFFFFFFFFu;
    Side side = Side::None;
    OrderState state = OrderState::New;
    std::uint8_t pad_ = 0;
};
static_assert(sizeof(EngineOrder) == 32);
static_assert(alignof(EngineOrder) == 8);

struct Fill
{
    ClientOrderId order_id = 0;
    ScaledPrice price = PRICE_NONE;
    std::int64_t qty = 0;
    NanoTime ts = 0;
    Side side = Side::None;
    bool full = false;
    bool maker = false;
    std::uint8_t pad_[4] = {};
};
static_assert(sizeof(Fill) == 40);
static_assert(alignof(Fill) == 8);

}
