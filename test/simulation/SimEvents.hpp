#pragma once

#include "common/MarketDataEvent.hpp"
#include "simulation/EngineView.hpp"
#include "simulation/HistoricalLOB.hpp"
#include "simulation/testing/EventBuilders.hpp"

#include <cstdint>

namespace cmf::sim
{

inline MarketDataEvent add(std::uint64_t id, Side side, std::int64_t price, std::uint32_t size)
{
    MarketDataEvent e{};
    e.order_id = id;
    e.action = Action::Add;
    e.side = side;
    e.price = price;
    e.size = size;
    return e;
}

inline MarketDataEvent cancel(std::uint64_t id, std::uint32_t remaining)
{
    MarketDataEvent e{};
    e.order_id = id;
    e.action = Action::Cancel;
    e.size = remaining;
    return e;
}

inline MarketDataEvent modify(std::uint64_t id, Side side, std::int64_t price, std::uint32_t size)
{
    MarketDataEvent e{};
    e.order_id = id;
    e.action = Action::Modify;
    e.side = side;
    e.price = price;
    e.size = size;
    return e;
}

inline MarketDataEvent trade(Side aggressor, std::int64_t price, std::uint32_t size,
                             std::uint32_t seq)
{
    MarketDataEvent e{};
    e.action = Action::Trade;
    e.side = aggressor;
    e.price = price;
    e.size = size;
    e.sequence = seq;
    return e;
}

inline MarketDataEvent fill(Side side, std::int64_t price, std::uint32_t size, std::uint32_t seq)
{
    MarketDataEvent e{};
    e.action = Action::Fill;
    e.side = side;
    e.price = price;
    e.size = size;
    e.sequence = seq;
    return e;
}

inline Trade aggress(Side aggressor, std::int64_t price, std::int64_t qty, std::uint32_t seq)
{
    return Trade{price, qty, 0, seq, aggressor, Action::Trade};
}

inline Trade fill_record(Side side, std::int64_t price, std::int64_t qty)
{
    return Trade{price, qty, 0, 0, side, Action::Fill};
}

inline EngineView<> make_view(const HistoricalLOB<>& h, FeeSchedule fees = {}, NanoTime latency = 0)
{
    return EngineView<>(h, 0, fees, latency);
}

}
