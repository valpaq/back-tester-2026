#pragma once

#include "common/MarketDataEvent.hpp"
#include "order_book/LimitOrderBook.hpp"
#include "simulation/HistoricalLOB.hpp"

#include <cstdint>
#include <unordered_map>

namespace cmf::sim
{

template <class BookType = LimitOrderBook>
class SimulatedRouter
{
  public:
    std::uint32_t apply(const MarketDataEvent& e)
    {
        if (e.instrument_id != 0 && e.order_id != 0)
            order_to_instrument_[e.order_id] = e.instrument_id;

        std::uint32_t instr = e.instrument_id;
        if (instr == 0 && e.order_id != 0)
        {
            const auto it = order_to_instrument_.find(e.order_id);
            if (it == order_to_instrument_.end())
            {
                ++unresolved_count_;
                return 0;
            }
            instr = it->second;
        }

        if (instr == 0)
            return 0;

        books_[instr].apply(e);
        return instr;
    }

    [[nodiscard]] HistoricalLOB<BookType>& basement(std::uint32_t instrument_id)
    {
        return books_[instrument_id];
    }

    [[nodiscard]] const HistoricalLOB<BookType>* find_basement(std::uint32_t instrument_id) const
    {
        const auto it = books_.find(instrument_id);
        return it == books_.end() ? nullptr : &it->second;
    }

    [[nodiscard]] std::size_t instrument_count() const noexcept { return books_.size(); }

    [[nodiscard]] std::uint64_t unresolved_count() const noexcept
    {
        return unresolved_count_;
    }

    void clear_all_chunk_trades()
    {
        for (auto& [id, book] : books_)
            book.clear_chunk_trades();
    }

  private:
    std::unordered_map<std::uint32_t, HistoricalLOB<BookType>> books_;
    std::unordered_map<std::uint64_t, std::uint32_t> order_to_instrument_;
    std::uint64_t unresolved_count_ = 0;
};

}
