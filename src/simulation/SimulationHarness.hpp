#pragma once

#include "common/MarketDataEvent.hpp"
#include "order_book/LimitOrderBook.hpp"
#include "simulation/EngineThreadPool.hpp"
#include "simulation/EngineView.hpp"
#include "simulation/Position.hpp"
#include "simulation/SimTypes.hpp"
#include "simulation/SimulatedLOB.hpp"
#include "simulation/SimulatedRouter.hpp"
#include "simulation/Strategy.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <unordered_map>
#include <vector>

namespace cmf::sim
{

template <class StrategyT, class BookType = LimitOrderBook>
    requires Strategy<StrategyT, BookType>
class SimulationHarness
{
  public:
    SimulationHarness(std::vector<std::uint32_t> instruments,
                      std::vector<StrategyT> strategies, std::size_t chunk_size,
                      FeeSchedule fees = {}, NanoTime latency = 0)
        : strategies_(std::move(strategies)),
          chunk_size_(chunk_size == 0 ? 1 : chunk_size), fees_(fees), latency_(latency),
          pool_(strategies_.size())
    {
        for (const std::uint32_t instr : instruments)
            register_instrument(instr);
        step_fn_ = [this](std::size_t e)
        { step_engine(e); };
    }

    SimulationHarness(const SimulationHarness&) = delete;
    SimulationHarness& operator=(const SimulationHarness&) = delete;
    SimulationHarness(SimulationHarness&&) = delete;
    SimulationHarness& operator=(SimulationHarness&&) = delete;

    void operator()(const MarketDataEvent& e)
    {
        const std::uint32_t instr = router_.apply(e);
        if (instr != 0)
        {
            auto it = instr_index_.find(instr);
            if (it == instr_index_.end())
                it = register_instrument(instr);
            NanoTime& t = instr_now_[it->second];
            if (e.ts_event > t)
                t = e.ts_event;
        }
        if (++pending_ >= chunk_size_)
            run_chunk();
    }

    void flush()
    {
        if (pending_ > 0)
            run_chunk();
    }

    [[nodiscard]] std::size_t engine_count() const noexcept { return strategies_.size(); }
    [[nodiscard]] std::size_t instrument_count() const noexcept { return instruments_.size(); }
    [[nodiscard]] const std::vector<std::uint32_t>& instruments() const noexcept
    {
        return instruments_;
    }

    [[nodiscard]] const EngineView<BookType>& engine_view(std::size_t engine,
                                                          std::size_t instrument_index) const
    {
        return views_[view_index(engine, instrument_index)];
    }

    [[nodiscard]] std::uint64_t total_fills() const noexcept
    {
        std::uint64_t n = 0;
        for (const auto& v : views_)
            n += v.fill_count();
        return n;
    }

    [[nodiscard]] double engine_net_pnl(std::size_t engine) const
    {
        double pnl = 0.0;
        for (std::size_t j = 0; j < instruments_.size(); ++j)
            pnl += engine_view(engine, j).position().net_pnl();
        return pnl;
    }

    [[nodiscard]] double total_net_pnl() const
    {
        double pnl = 0.0;
        for (const auto& v : views_)
            pnl += v.position().net_pnl();
        return pnl;
    }

    [[nodiscard]] const SimulatedRouter<BookType>& router() const noexcept { return router_; }

  private:
    std::unordered_map<std::uint32_t, std::size_t>::iterator register_instrument(std::uint32_t id)
    {
        const auto [it, inserted] = instr_index_.emplace(id, instruments_.size());
        if (!inserted)
            return it;
        instruments_.push_back(id);
        instr_now_.push_back(0);
        HistoricalLOB<BookType>& basement = router_.basement(id);
        for (std::size_t e = 0; e < strategies_.size(); ++e)
            views_.emplace_back(basement, static_cast<EngineId>(e), fees_, latency_);
        return it;
    }

    [[nodiscard]] std::size_t view_index(std::size_t engine,
                                         std::size_t instrument_index) const noexcept
    {
        return instrument_index * strategies_.size() + engine;
    }

    EngineView<BookType>& view_at(std::size_t engine, std::size_t instrument_index)
    {
        return views_[view_index(engine, instrument_index)];
    }

    void step_engine(std::size_t engine)
    {
        for (std::size_t j = 0; j < instruments_.size(); ++j)
        {
            const NanoTime t = instr_now_[j];
            EngineView<BookType>& v = view_at(engine, j);
            v.clear_fills();
            v.reconcile_consumed();
            v.process_trades(v.basement().chunk_trades());
            v.activate_due(t);
            SimulatedLOB<BookType> sim(v);
            SimContext<BookType> ctx{sim, v, instruments_[j], t};
            strategies_[engine].on_chunk(ctx);
        }
    }

    void run_chunk()
    {
        pool_.run(strategies_.size(), step_fn_);
        router_.clear_all_chunk_trades();
        pending_ = 0;
    }

    std::vector<std::uint32_t> instruments_;
    std::vector<StrategyT> strategies_;
    std::size_t chunk_size_;
    FeeSchedule fees_;
    NanoTime latency_ = 0;
    SimulatedRouter<BookType> router_;
    std::deque<EngineView<BookType>> views_;
    EngineThreadPool pool_;
    std::function<void(std::size_t)> step_fn_;
    std::vector<NanoTime> instr_now_;
    std::unordered_map<std::uint32_t, std::size_t> instr_index_;
    std::size_t pending_ = 0;
};

}
