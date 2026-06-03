#pragma once

#include "order_book/LimitOrderBook.hpp"
#include "order_book/OrderBook.hpp"
#include "simulation/EngineView.hpp"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace cmf::sim
{

template <class BookType = LimitOrderBook>
class SimulatedLOB : public OrderBook<SimulatedLOB<BookType>>
{
    friend class OrderBook<SimulatedLOB<BookType>>;

  public:
    explicit SimulatedLOB(const EngineView<BookType>& view) : view_(&view) {}

  protected:
    using LevelPair = std::pair<ScaledPrice, ScaledPrice>;

    [[nodiscard]] std::optional<ScaledPrice> best_price_impl(Side side) const noexcept
    {
        return view_->composed_best(side);
    }

    [[nodiscard]] std::uint64_t volume_at_impl(Side side, ScaledPrice p) const noexcept
    {
        const std::int64_t net = view_->net_basement_qty(side, p);
        return static_cast<std::uint64_t>(net + view_->own_qty(side, p));
    }

    [[nodiscard]] bool empty_impl(Side side) const noexcept
    {
        return !view_->composed_best(side);
    }

    [[nodiscard]] std::span<const LevelPair> side_levels_impl(Side side) const
    {
        rebuild(side);
        return {cache_};
    }

  private:
    void rebuild(Side side) const
    {
        cache_.clear();
        hist_scratch_.clear();
        view_->basement().for_each_level(side, [&](ScaledPrice p, std::int64_t q)
                                         {
            const std::int64_t net = view_->net_basement_qty(side, p, q);
            if (net > 0)
                hist_scratch_.emplace_back(p, net); });

        const auto asc = view_->own_levels_asc(side);
        const std::size_t own_n = asc.size();
        const auto own_at = [&](std::size_t j) -> LevelPair
        {
            const auto& l = (side == Side::Buy) ? asc[own_n - 1 - j] : asc[j];
            return {l.first, l.second};
        };

        const auto& hist = hist_scratch_;
        std::size_t i = 0, j = 0;
        while (i < hist.size() || j < own_n)
        {
            if (j >= own_n)
                cache_.push_back(hist[i++]);
            else if (i >= hist.size())
                cache_.push_back(own_at(j++));
            else if (const LevelPair o = own_at(j); hist[i].first == o.first)
            {
                cache_.emplace_back(hist[i].first, hist[i].second + o.second);
                ++i;
                ++j;
            }
            else if (price_is_better(side, hist[i].first, o.first))
                cache_.push_back(hist[i++]);
            else
                cache_.push_back(own_at(j++));
        }
    }

    const EngineView<BookType>* view_;
    mutable std::vector<LevelPair> cache_;
    mutable std::vector<LevelPair> hist_scratch_;
};

}
