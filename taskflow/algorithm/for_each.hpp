#pragma once

#include "../taskflow.hpp"

namespace tf {

// Function: make_for_each_task
template <typename B, typename E, typename C, typename P = DefaultPartitioner>
auto make_for_each_task(B b, E e, C c, P part = P()) {
  
  using namespace std::string_literals;

  using B_t = std::decay_t<unwrap_ref_decay_t<B>>;
  using E_t = std::decay_t<unwrap_ref_decay_t<E>>;

  return [=] (Runtime& rt) mutable {

    // fetch the stateful values
    B_t beg = b;
    E_t end = e;

    size_t W = rt.executor().num_workers();
    size_t N = std::distance(beg, end);

    // the workload is sequentially doable
    if(W <= 1 || N <= part.chunk_size()) {
      part([=](){ std::for_each(beg, end, c); })();
      return;
    }
    
    PreemptionGuard preemption_guard(rt);
    
    // use no more workers than the iteration count
    if(N < W) {
      W = N;
    }
    
    // static partitioner
    if constexpr(part.type() == PartitionerType::STATIC) {
      size_t chunk_size;
      for(size_t w=0, curr_b=0; w<W && curr_b < N;) {
        chunk_size = part.adjusted_chunk_size(N, W, w);
        auto task = part([=] () mutable {
          part.loop(N, W, curr_b, chunk_size,
            [=, prev_e=size_t{0}](size_t part_b, size_t part_e) mutable {
              std::advance(beg, part_b - prev_e);
              for(size_t x = part_b; x<part_e; x++) {
                c(*beg++);
              }
              prev_e = part_e;
            }
          ); 
        });
        (++w == W || (curr_b += chunk_size) >= N) ? task() : rt.silent_async(task);
      }
    }
    // dynamic partitioner
    else {
      auto next = std::make_shared<std::atomic<size_t>>(0);
      for(size_t w=0; w<W;) {
        auto task = part([=] () mutable {
          part.loop(N, W, *next, 
            [=, prev_e=size_t{0}](size_t part_b, size_t part_e) mutable {
              std::advance(beg, part_b - prev_e);
              for(size_t x = part_b; x<part_e; x++) {
                c(*beg++);
              }
              prev_e = part_e;
            }
          );
        });
        (++w == W) ? task() : rt.silent_async(task);
      }
    }

  };
}

// Function: make_for_each_index_task
template <typename B, typename E, typename S, typename C, typename P = DefaultPartitioner>
auto make_for_each_index_task(B b, E e, S s, C c, P part = P()){
  
  using namespace std::string_literals;

  using B_t = std::decay_t<unwrap_ref_decay_t<B>>;
  using E_t = std::decay_t<unwrap_ref_decay_t<E>>;
  using S_t = std::decay_t<unwrap_ref_decay_t<S>>;

  return [=] (Runtime& rt) mutable {

    // fetch the iterator values
    B_t beg = b;
    E_t end = e;
    S_t inc = s;
    
    // nothing to be done if the range is invalid
    if(is_range_invalid(beg, end, inc)) {
      return;
    }

    size_t W = rt.executor().num_workers();
    size_t N = distance(beg, end, inc);

    // only myself - no need to spawn another graph
    if(W <= 1 || N <= part.chunk_size()) {
      part([&](){
        for(size_t x=0; x<N; x++, beg+=inc) {
          c(beg);
        }
      })();
      return;
    }

    PreemptionGuard preemption_guard(rt);
    
    if(N < W) {
      W = N;
    }
    
    // static partitioner
    if constexpr(part.type() == PartitionerType::STATIC) {
      size_t chunk_size;
      for(size_t w=0, curr_b=0; w<W && curr_b < N;) {
        chunk_size = part.adjusted_chunk_size(N, W, w);
        auto task = part([=] () mutable {
          part.loop(N, W, curr_b, chunk_size, [=] (size_t part_b, size_t part_e) {
            auto idx = static_cast<B_t>(part_b) * inc + beg;
            for(size_t x=part_b; x<part_e; x++, idx += inc) {
              c(idx);
            }
          });
        });
        (++w == W || (curr_b += chunk_size) >= N) ? task() : rt.silent_async(task);
      }
    }
    // dynamic partitioner
    else {
      auto next = std::make_shared<std::atomic<size_t>>(0);
      for(size_t w=0; w<W;) {
        auto task = part([=] () mutable {
          part.loop(N, W, *next, [=] (size_t part_b, size_t part_e) {
            auto idx = static_cast<B_t>(part_b) * inc + beg;
            for(size_t x=part_b; x<part_e; x++, idx += inc) {
              c(idx);
            }
          });
        });
        (++w == W) ? task() : rt.silent_async(task);
      }
    }
  };
}

// ----------------------------------------------------------------------------
// for_each
// ----------------------------------------------------------------------------

// Function: for_each
template <typename B, typename E, typename C, typename P>
Task FlowBuilder::for_each(B beg, E end, C c, P part) {
  return emplace(
    make_for_each_task(beg, end, c, part)
  );
}

// ----------------------------------------------------------------------------
// for_each_index
// ----------------------------------------------------------------------------

// Function: for_each_index
template <typename B, typename E, typename S, typename C, typename P>
Task FlowBuilder::for_each_index(B beg, E end, S inc, C c, P part){
  return emplace(
    make_for_each_index_task(beg, end, inc, c, part)
  );
}


}  // end of namespace tf -----------------------------------------------------

