#pragma once

#include "common/MarketDataEvent.hpp"
#include "order_book/LimitOrderBook.hpp"
#include "simulation/HistoricalLOB.hpp"
#include "simulation/Position.hpp"
#include "simulation/SimTypes.hpp"

#include "absl/container/inlined_vector.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cmf::sim
{

class PriceLadder
{
  public:
    using Level = std::pair<ScaledPrice, std::int64_t>;
    using Storage = absl::InlinedVector<Level, 8>;

    void add(ScaledPrice price, std::int64_t delta)
    {
        if (delta == 0)
            return;
        const auto it = lower(price);
        if (it != v_.end() && it->first == price)
        {
            it->second += delta;
            if (it->second == 0)
                v_.erase(it);
        }
        else
        {
            v_.insert(it, Level{price, delta});
        }
    }

    void set(ScaledPrice price, std::int64_t value)
    {
        const auto it = lower(price);
        if (it != v_.end() && it->first == price)
        {
            if (value == 0)
                v_.erase(it);
            else
                it->second = value;
        }
        else if (value != 0)
        {
            v_.insert(it, Level{price, value});
        }
    }

    [[nodiscard]] std::int64_t get(ScaledPrice price) const noexcept
    {
        const auto it = lower(price);
        return (it != v_.end() && it->first == price) ? it->second : 0;
    }

    [[nodiscard]] std::span<const Level> ascending() const noexcept
    {
        return {v_.data(), v_.size()};
    }

  private:
    [[nodiscard]] Storage::iterator lower(ScaledPrice price)
    {
        return std::lower_bound(v_.begin(), v_.end(), price,
                                [](const Level& a, ScaledPrice k)
                                { return a.first < k; });
    }
    [[nodiscard]] Storage::const_iterator lower(ScaledPrice price) const
    {
        return std::lower_bound(v_.begin(), v_.end(), price,
                                [](const Level& a, ScaledPrice k)
                                { return a.first < k; });
    }

    Storage v_;
};

template <class BookType = LimitOrderBook>
class alignas(64) EngineView
{
  public:
    explicit EngineView(const HistoricalLOB<BookType>& basement, EngineId id = 0,
                        FeeSchedule fees = {}, NanoTime latency = 0)
        : basement_(&basement), id_(id), fees_(fees),
          latency_(std::max<NanoTime>(0, latency))
    {
    }

    OrderHandle submit(Side side, ScaledPrice limit, std::int64_t qty, TimeInForce tif,
                       NanoTime now)
    {
        if (qty <= 0)
            return OrderHandle{0};
        if (side == Side::None || limit == PRICE_NONE || limit == UNDEF_PRICE)
            return OrderHandle{0};

        if (latency_ <= 0)
        {
            if (tif == TimeInForce::FillOrKill && fillable(side, limit, qty) < qty)
                return OrderHandle{0};
            const ClientOrderId oid = ++last_id_;
            execute(oid, side, limit, qty, tif, now);
            return OrderHandle{oid};
        }

        const ClientOrderId oid = ++last_id_;
        pending_.push_back(Pending{oid, side, limit, qty, tif, now + latency_});
        return OrderHandle{oid};
    }

    void activate_due(NanoTime current)
    {
        if (pending_.empty())
            return;
        std::size_t kept = 0;
        for (std::size_t i = 0; i < pending_.size(); ++i)
        {
            const Pending p = pending_[i];
            if (p.effective <= current)
                execute(p.id, p.side, p.limit, p.qty, p.tif, current);
            else
                pending_[kept++] = p;
        }
        pending_.resize(kept);
    }

    OrderHandle submit_limit(Side side, ScaledPrice limit, std::int64_t qty, NanoTime now)
    {
        return submit(side, limit, qty, TimeInForce::GoodTillCancel, now);
    }

    OrderHandle submit_market(Side side, std::int64_t qty, NanoTime now,
                              TimeInForce tif = TimeInForce::FillAndKill)
    {
        if (tif == TimeInForce::GoodTillCancel)
            tif = TimeInForce::FillAndKill;
        const ScaledPrice limit = (side == Side::Buy) ? UNDEF_PRICE - 1 : PRICE_NONE + 1;
        return submit(side, limit, qty, tif, now);
    }

    bool cancel(ClientOrderId oid)
    {
        for (std::size_t i = 0; i < pending_.size(); ++i)
            if (pending_[i].id == oid)
            {
                pending_.erase(pending_.begin() + static_cast<std::ptrdiff_t>(i));
                return true;
            }

        const auto it = index_.find(oid);
        if (it == index_.end())
            return false;
        const std::uint32_t slot = it->second;
        EngineOrder& o = arena_[slot];
        const int si = side_idx(o.side);
        own_[si].add(o.price, -o.quantity);
        fifo_remove(si, o.price, slot);
        invalidate(o.side);
        o.state = OrderState::Cancelled;
        free_slot(slot);
        index_.erase(it);
        return true;
    }

    void process_trades(std::span<const Trade> trades)
    {
        for (const Trade& t : trades)
        {
            if (t.action != Action::Trade || t.side == Side::None || t.qty <= 0)
                continue;
            passive_fill_level(opposite(t.side), t.price, t.qty, t.ts_event);
        }
    }

    void reconcile_consumed()
    {
        for (const Side s : {Side::Buy, Side::Sell})
        {
            const int si = side_idx(s);
            clamp_to_basement(consumed_[si], s);
            clamp_to_basement(ahead_[si], s);
        }
    }

    [[nodiscard]] const HistoricalLOB<BookType>& basement() const noexcept
    {
        return *basement_;
    }
    [[nodiscard]] std::int64_t own_qty(Side s, ScaledPrice p) const noexcept
    {
        return own_[side_idx(s)].get(p);
    }
    [[nodiscard]] std::int64_t consumed_qty(Side s, ScaledPrice p) const noexcept
    {
        return consumed_[side_idx(s)].get(p);
    }
    [[nodiscard]] std::span<const PriceLadder::Level> own_levels_asc(Side s) const noexcept
    {
        return own_[side_idx(s)].ascending();
    }

    [[nodiscard]] std::int64_t net_basement_qty(Side s, ScaledPrice p,
                                                std::int64_t hist) const noexcept
    {
        return std::max<std::int64_t>(0, hist - consumed_qty(s, p));
    }
    [[nodiscard]] std::int64_t net_basement_qty(Side s, ScaledPrice p) const noexcept
    {
        return net_basement_qty(s, p, static_cast<std::int64_t>(basement_->volume_at(s, p)));
    }

    [[nodiscard]] std::optional<ScaledPrice> composed_best(Side s) const
    {
        const int i = side_idx(s);
        const std::uint64_t v = basement_->version(s);
        if (hot_valid_[i] && hot_version_[i] == v)
            return hot_price_[i] == PRICE_NONE ? std::nullopt
                                               : std::optional<ScaledPrice>{hot_price_[i]};
        const std::optional<ScaledPrice> r = recompute_best(s);
        hot_price_[i] = r ? *r : PRICE_NONE;
        hot_version_[i] = v;
        hot_valid_[i] = true;
        return r;
    }

    [[nodiscard]] std::span<const Fill> fills() const noexcept { return {fills_}; }
    void clear_fills() noexcept { fills_.clear(); }
    [[nodiscard]] std::uint64_t fill_count() const noexcept { return fill_count_; }
    [[nodiscard]] EngineId id() const noexcept { return id_; }
    [[nodiscard]] const Position& position() const noexcept { return position_; }

  private:
    static constexpr std::uint32_t NIL = 0xFFFFFFFFu;

    struct Pending
    {
        ClientOrderId id = 0;
        Side side = Side::None;
        ScaledPrice limit = PRICE_NONE;
        std::int64_t qty = 0;
        TimeInForce tif = TimeInForce::GoodTillCancel;
        NanoTime effective = 0;
    };

    void emit_fill(const Fill& f)
    {
        ++fill_count_;
        fills_.push_back(f);
    }

    void account_and_emit(ClientOrderId oid, Side side, ScaledPrice price, std::int64_t f,
                          NanoTime ts, bool full, bool maker)
    {
        const double px = to_price(price);
        position_.apply_fill(side, f, px);
        position_.fees += fees_.fee(static_cast<double>(f) * px, maker);
        emit_fill(Fill{oid, price, f, ts, side, full, maker});
    }

    [[nodiscard]] std::uint32_t acquire_slot()
    {
        if (free_head_ != NIL)
        {
            const std::uint32_t slot = free_head_;
            free_head_ = arena_[slot].next_free;
            return slot;
        }
        const auto slot = static_cast<std::uint32_t>(arena_.size());
        arena_.push_back(EngineOrder{});
        return slot;
    }

    void free_slot(std::uint32_t slot) noexcept
    {
        arena_[slot].next_free = free_head_;
        free_head_ = slot;
    }

    void fifo_remove(int si, ScaledPrice price, std::uint32_t slot)
    {
        const auto fit = resting_[si].find(price);
        if (fit == resting_[si].end())
            return;
        auto& fifo = fit->second;
        const auto sit = std::find(fifo.begin(), fifo.end(), slot);
        assert(sit != fifo.end());
        if (sit == fifo.end())
            return;
        fifo.erase(sit);
        if (fifo.empty())
            resting_[si].erase(fit);
    }

    [[nodiscard]] std::int64_t fillable(Side side, ScaledPrice limit, std::int64_t qty) const
    {
        const Side opp = opposite(side);
        std::int64_t total = 0;
        basement_->for_levels_until(opp, [&](ScaledPrice p, std::int64_t q)
                                    {
            if (!crosses(side, limit, p))
                return false;
            total += net_basement_qty(opp, p, q);
            return total < qty; });
        return total;
    }

    std::int64_t walk_fill(ClientOrderId oid, Side side, ScaledPrice limit,
                           std::int64_t qty, NanoTime now)
    {
        const Side opp = opposite(side);
        const int oi = side_idx(opp);
        std::int64_t remaining = qty;

        walk_scratch_.clear();
        basement_->for_levels_until(opp, [&](ScaledPrice p, std::int64_t q)
                                    {
            if (remaining <= 0)
                return false;
            if (!crosses(side, limit, p))
                return false;
            const std::int64_t avail = net_basement_qty(opp, p, q);
            const std::int64_t f = std::min(remaining, avail);
            if (f > 0)
            {
                walk_scratch_.emplace_back(p, f);
                remaining -= f;
            }
            return remaining > 0; });

        for (std::size_t i = 0; i < walk_scratch_.size(); ++i)
        {
            const auto [p, f] = walk_scratch_[i];
            consumed_[oi].add(p, f);
            invalidate(opp);
            const bool full = (remaining == 0) && (i + 1 == walk_scratch_.size());
            account_and_emit(oid, side, p, f, now, full, false);
        }
        return qty - remaining;
    }

    void execute(ClientOrderId oid, Side side, ScaledPrice limit, std::int64_t qty,
                 TimeInForce tif, NanoTime t)
    {
        if (tif == TimeInForce::FillOrKill && fillable(side, limit, qty) < qty)
            return;
        const std::int64_t filled = walk_fill(oid, side, limit, qty, t);
        if (qty - filled > 0 && tif == TimeInForce::GoodTillCancel)
            rest(oid, side, limit, qty - filled);
    }

    [[nodiscard]] std::optional<ScaledPrice> recompute_best(Side s) const
    {
        std::optional<ScaledPrice> hist;
        basement_->for_levels_until(s, [&](ScaledPrice p, std::int64_t q)
                                    {
            if (net_basement_qty(s, p, q) > 0)
            {
                hist = p;
                return false;
            }
            return true; });
        std::optional<ScaledPrice> own;
        const auto& asc = own_[side_idx(s)].ascending();
        if (!asc.empty())
            own = (s == Side::Buy) ? asc.back().first : asc.front().first;

        if (!hist)
            return own;
        if (!own)
            return hist;
        return price_is_better(s, *hist, *own) ? hist : own;
    }

    void invalidate(Side s) noexcept { hot_valid_[side_idx(s)] = false; }

    void clamp_to_basement(PriceLadder& ladder, Side s)
    {
        const auto sp = ladder.ascending();
        for (std::size_t i = sp.size(); i-- > 0;)
        {
            const auto vol = static_cast<std::int64_t>(basement_->volume_at(s, sp[i].first));
            if (sp[i].second > vol)
                ladder.set(sp[i].first, vol);
        }
    }

    void rest(ClientOrderId oid, Side side, ScaledPrice price, std::int64_t qty)
    {
        const std::uint32_t slot = acquire_slot();
        EngineOrder& o = arena_[slot];
        o.price = price;
        o.quantity = qty;
        o.order_id = oid;
        o.next_free = NIL;
        o.side = side;
        o.state = OrderState::Resting;

        const int si = side_idx(side);
        if (own_[si].get(price) == 0)
            ahead_[si].set(price, net_basement_qty(side, price));
        own_[si].add(price, qty);
        resting_[si][price].push_back(slot);
        index_[oid] = slot;
        invalidate(side);
    }

    void passive_fill_level(Side side, ScaledPrice price, std::int64_t vol, NanoTime ts)
    {
        const int si = side_idx(side);
        if (own_[si].get(price) == 0)
            return;
        invalidate(side);

        const std::int64_t ahead = ahead_[si].get(price);
        const std::int64_t depleted = std::min(vol, ahead);
        ahead_[si].add(price, -depleted);
        if (depleted > 0)
            consumed_[si].add(price, depleted);
        vol -= depleted;
        if (vol <= 0)
            return;

        const auto fit = resting_[si].find(price);
        if (fit == resting_[si].end())
            return;
        std::deque<std::uint32_t>& fifo = fit->second;
        while (!fifo.empty() && vol > 0)
        {
            const std::uint32_t slot = fifo.front();
            EngineOrder& o = arena_[slot];
            const std::int64_t f = std::min(vol, o.quantity);
            o.quantity -= f;
            vol -= f;
            own_[si].add(price, -f);
            const bool full = o.quantity == 0;
            account_and_emit(o.order_id, side, price, f, ts, full, true);
            if (!full)
            {
                o.state = OrderState::PartiallyFilled;
                break;
            }
            index_.erase(o.order_id);
            o.state = OrderState::Filled;
            fifo.pop_front();
            free_slot(slot);
        }
        if (fifo.empty())
            resting_[si].erase(fit);
    }

    const HistoricalLOB<BookType>* basement_;
    EngineId id_;
    FeeSchedule fees_;
    NanoTime latency_ = 0;
    std::vector<Pending> pending_;
    Position position_;
    std::array<PriceLadder, 2> own_;
    std::array<PriceLadder, 2> consumed_;
    std::array<PriceLadder, 2> ahead_;
    std::vector<EngineOrder> arena_;
    std::unordered_map<ClientOrderId, std::uint32_t> index_;
    std::array<std::unordered_map<ScaledPrice, std::deque<std::uint32_t>>, 2> resting_;
    std::uint32_t free_head_ = NIL;
    ClientOrderId last_id_ = 0;
    std::vector<Fill> fills_;
    std::vector<std::pair<ScaledPrice, std::int64_t>> walk_scratch_;
    std::uint64_t fill_count_ = 0;

    mutable std::array<ScaledPrice, 2> hot_price_{PRICE_NONE, PRICE_NONE};
    mutable std::array<std::uint64_t, 2> hot_version_{};
    mutable std::array<bool, 2> hot_valid_{};
};

}
