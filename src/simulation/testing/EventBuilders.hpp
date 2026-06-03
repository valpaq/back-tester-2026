#pragma once

#include "common/MarketDataEvent.hpp"

#include <cstdint>

namespace cmf::sim
{

inline MarketDataEvent ev(std::uint32_t instrument, std::uint64_t id, Action action, Side side,
                          std::int64_t price, std::uint32_t size, NanoTime ts = 0)
{
    MarketDataEvent e{};
    e.instrument_id = instrument;
    e.order_id = id;
    e.action = action;
    e.side = side;
    e.price = price;
    e.size = size;
    e.ts_event = ts;
    return e;
}

}
