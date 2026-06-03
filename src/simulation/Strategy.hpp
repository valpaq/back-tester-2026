#pragma once

#include "common/BasicTypes.hpp"
#include "order_book/LimitOrderBook.hpp"
#include "simulation/EngineView.hpp"
#include "simulation/SimTypes.hpp"
#include "simulation/SimulatedLOB.hpp"

#include <concepts>
#include <cstdint>
#include <optional>

namespace cmf::sim
{

template <class BookType = LimitOrderBook>
class SimContext
{
  public:
    SimContext(const SimulatedLOB<BookType>& book, EngineView<BookType>& view,
               std::uint32_t instrument, NanoTime now) noexcept
        : book_(book), view_(view), instrument_(instrument), now_(now)
    {
    }

    [[nodiscard]] std::uint32_t instrument() const noexcept { return instrument_; }
    [[nodiscard]] NanoTime now() const noexcept { return now_; }

    OrderHandle submit_limit(Side side, ScaledPrice price, std::int64_t qty)
    {
        return view_.submit_limit(side, price, qty, now_);
    }
    bool cancel(ClientOrderId id) { return view_.cancel(id); }

    [[nodiscard]] std::optional<ScaledPrice> best(Side side) const
    {
        return book_.best_price(side);
    }
    [[nodiscard]] std::optional<ScaledPrice> mid() const
    {
        const auto b = book_.best_price(Side::Buy);
        const auto a = book_.best_price(Side::Sell);
        if (b && a)
            return (*b + *a) / 2;
        return std::nullopt;
    }
    [[nodiscard]] std::int64_t own_qty(Side side, ScaledPrice price) const noexcept
    {
        return view_.own_qty(side, price);
    }
    [[nodiscard]] const Position& position() const noexcept { return view_.position(); }

  private:
    const SimulatedLOB<BookType>& book_;
    EngineView<BookType>& view_;
    std::uint32_t instrument_;
    NanoTime now_;
};

template <class S, class BookType = LimitOrderBook>
concept Strategy = requires(S s, SimContext<BookType>& ctx) {
    { s.on_chunk(ctx) } -> std::same_as<void>;
};

struct NaiveTaker
{
    ScaledPrice limit = 0;
    std::int64_t qty = 1;

    template <class Ctx>
    void on_chunk(Ctx& c)
    {
        if (const auto ask = c.best(Side::Sell); ask && *ask <= limit)
            c.submit_limit(Side::Buy, *ask, qty);
    }
};

struct PassiveMaker
{
    ScaledPrice price = 0;
    std::int64_t qty = 1;

    template <class Ctx>
    void on_chunk(Ctx& c)
    {
        c.submit_limit(Side::Buy, price, qty);
    }
};

}
