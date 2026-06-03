#pragma once

#include "common/MarketDataEvent.hpp"
#include "order_book/LimitOrderBook.hpp"
#include "simulation/SimTypes.hpp"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace cmf::sim
{

struct Trade
{
    ScaledPrice price = PRICE_NONE;
    std::int64_t qty = 0;
    NanoTime ts_event = 0;
    std::uint32_t sequence = 0;
    Side side = Side::None;
    Action action = Action::None;
};

template <class BookType = LimitOrderBook>
class HistoricalLOB
{
  public:
    HistoricalLOB() = default;
    HistoricalLOB(const HistoricalLOB&) = delete;
    HistoricalLOB& operator=(const HistoricalLOB&) = delete;
    HistoricalLOB(HistoricalLOB&&) noexcept = default;
    HistoricalLOB& operator=(HistoricalLOB&&) noexcept = default;

    void apply(const MarketDataEvent& e)
    {
        if (has_flag(e.flags, Flags::BadTsRecv) || has_flag(e.flags, Flags::MaybeBadBook))
            return;

        switch (e.action)
        {
        case Action::Trade:
        case Action::Fill:
            if (!has_flag(e.flags, Flags::Tob) && !has_flag(e.flags, Flags::Mbp) &&
                !has_flag(e.flags, Flags::Snapshot))
                chunk_trades_.push_back(Trade{e.price, static_cast<std::int64_t>(e.size),
                                              e.ts_event, e.sequence, e.side, e.action});
            return;
        case Action::Add:
        case Action::Modify:
        case Action::Cancel:
        case Action::Clear:
            break;
        case Action::None:
            return;
        }

        book_.apply(e);
        const Top bid_now = top(Side::Buy);
        if (bid_now != last_bid_)
        {
            ++version_bid_;
            last_bid_ = bid_now;
        }
        const Top ask_now = top(Side::Sell);
        if (ask_now != last_ask_)
        {
            ++version_ask_;
            last_ask_ = ask_now;
        }
    }

    [[nodiscard]] std::optional<ScaledPrice> best_price(Side s) const noexcept
    {
        return book_.best_price(s);
    }
    [[nodiscard]] std::uint64_t volume_at(Side s, ScaledPrice p) const noexcept
    {
        return static_cast<std::uint64_t>(
            std::max<std::int64_t>(0, static_cast<std::int64_t>(book_.volume_at(s, p))));
    }
    [[nodiscard]] bool empty(Side s) const noexcept { return book_.empty(s); }

    template <class F>
    void for_each_level(Side s, F&& fn) const
    {
        book_.for_each_level(s, std::forward<F>(fn));
    }

    template <class F>
    void for_levels_until(Side s, F&& fn) const
    {
        book_.for_levels_until(s, std::forward<F>(fn));
    }

    [[nodiscard]] std::uint64_t version(Side s) const noexcept
    {
        return s == Side::Buy ? version_bid_ : version_ask_;
    }

    [[nodiscard]] std::span<const Trade> chunk_trades() const noexcept
    {
        return {chunk_trades_};
    }
    void clear_chunk_trades() noexcept { chunk_trades_.clear(); }

  private:
    using Top = std::pair<std::optional<ScaledPrice>, std::uint64_t>;

    [[nodiscard]] Top top(Side s) const noexcept
    {
        const auto p = book_.best_price(s);
        return {p, p ? volume_at(s, *p) : 0};
    }

    BookType book_;
    std::vector<Trade> chunk_trades_;
    Top last_bid_{std::nullopt, 0};
    Top last_ask_{std::nullopt, 0};
    std::uint64_t version_bid_ = 0;
    std::uint64_t version_ask_ = 0;
};

}
