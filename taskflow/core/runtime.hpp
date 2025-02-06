#pragma once

#include "executor.hpp"

namespace tf {

/**
@class Runtime

@brief class to include a runtime object in a task

A runtime object allows users to interact with the
scheduling runtime inside a task, such as scheduling an active task,
spawning a subflow, and so on.

@code{.cpp}
tf::Task A, B, C, D;
std::tie(A, B, C, D) = taskflow.emplace(
  [] () { return 0; },
  [&C] (tf::Runtime& rt) {  // C must be captured by reference
    std::cout << "B\n";
    rt.schedule(C);
  },
  [] () { std::cout << "C\n"; },
  [] () { std::cout << "D\n"; }
);
A.precede(B, C, D);
executor.run(taskflow).wait();
@endcode

A runtime object is associated with the worker and the executor
that runs the task.

*/
class Runtime {

  friend class Executor;
  friend class FlowBuilder;
  friend class PreemptionGuard;
  friend class Algorithm;
  
  #define TF_RUNTIME_CHECK_CALLER(msg)                                          \
  if(pt::this_worker == nullptr || pt::this_worker->_executor != &_executor) {  \
    TF_THROW(msg);                                                              \
  }

  public:
  
  /**
  @brief obtains the running executor

  The running executor of a runtime task is the executor that runs
  the parent taskflow of that runtime task.

  @code{.cpp}
  tf::Executor executor;
  tf::Taskflow taskflow;
  taskflow.emplace([&](tf::Runtime& rt){
    assert(&(rt.executor()) == &executor);
  });
  executor.run(taskflow).wait();
  @endcode
  */
  Executor& executor();
  
  /**
  @brief acquire a reference to the underlying worker
  */
  inline Worker& worker();

  /**
  @brief schedules an active task immediately to the worker's queue

  @param task the given active task to schedule immediately

  This member function immediately schedules an active task to the
  task queue of the associated worker in the runtime task.
  An active task is a task in a running taskflow.
  The task may or may not be running, and scheduling that task
  will immediately put the task into the task queue of the worker
  that is running the runtime task.
  Consider the following example:

  @code{.cpp}
  tf::Task A, B, C, D;
  std::tie(A, B, C, D) = taskflow.emplace(
    [] () { return 0; },
    [&C] (tf::Runtime& rt) {  // C must be captured by reference
      std::cout << "B\n";
      rt.schedule(C);
    },
    [] () { std::cout << "C\n"; },
    [] () { std::cout << "D\n"; }
  );
  A.precede(B, C, D);
  executor.run(taskflow).wait();
  @endcode

  The executor will first run the condition task @c A which returns @c 0
  to inform the scheduler to go to the runtime task @c B.
  During the execution of @c B, it directly schedules task @c C without
  going through the normal taskflow graph scheduling process.
  At this moment, task @c C is active because its parent taskflow is running.
  When the taskflow finishes, we will see both @c B and @c C in the output.
  */
  void schedule(Task task);
  
  /**
  @brief runs the given callable asynchronously

  @tparam F callable type
  @param f callable object
    
  The method creates an asynchronous task to launch the given
  function on the given arguments.
  The difference to tf::Executor::async is that the created asynchronous task
  pertains to the runtime object.
  Applications can explicitly issue tf::Runtime::corun_all
  to wait for all spawned asynchronous tasks to finish.
  For example:

  @code{.cpp}
  std::atomic<int> counter(0);
  taskflow.emplace([&](tf::Runtime& rt){
    auto fu1 = rt.async([&](){ counter++; });
    auto fu2 = rt.async([&](){ counter++; });
    fu1.get();
    fu2.get();
    assert(counter == 2);
    
    // spawn 100 asynchronous tasks from the worker of the runtime
    for(int i=0; i<100; i++) {
      rt.async([&](){ counter++; });
    }
    
    // wait for the 100 asynchronous tasks to finish
    rt.corun_all();
    assert(counter == 102);
  });
  @endcode

  This method is thread-safe and can be called by multiple workers
  that hold the reference to the runtime.
  For example, the code below spawns 100 tasks from the worker of
  a runtime, and each of the 100 tasks spawns another task
  that will be run by another worker.
  
  @code{.cpp}
  std::atomic<int> counter(0);
  taskflow.emplace([&](tf::Runtime& rt){
    // worker of the runtime spawns 100 tasks each spawning another task
    // that will be run by another worker
    for(int i=0; i<100; i++) {
      rt.async([&](){ 
        counter++; 
        rt.async([](){ counter++; });
      });
    }
    
    // wait for the 200 asynchronous tasks to finish
    rt.corun_all();
    assert(counter == 200);
  });
  @endcode
  */
  template <typename F>
  auto async(F&& f);
  
  /**
  @brief runs the given callable asynchronously

  @tparam F callable type
  @tparam P task parameters type

  @param params task parameters
  @param f callable

  <p><!-- Doxygen warning workaround --></p>

  @code{.cpp}
  taskflow.emplace([&](tf::Runtime& rt){
    auto future = rt.async("my task", [](){});
    future.get();
  });
  @endcode

  */
  template <typename P, typename F>
  auto async(P&& params, F&& f);

  /**
  @brief runs the given function asynchronously without returning any future object

  @tparam F callable type
  @param f callable

  This member function is more efficient than tf::Runtime::async
  and is encouraged to use when there is no data returned.

  @code{.cpp}
  std::atomic<int> counter(0);
  taskflow.emplace([&](tf::Runtime& rt){
    for(int i=0; i<100; i++) {
      rt.silent_async([&](){ counter++; });
    }
    rt.corun_all();
    assert(counter == 100);
  });
  @endcode

  This member function is thread-safe.
  */
  template <typename F>
  void silent_async(F&& f);
  
  /**
  @brief runs the given function asynchronously without returning any future object

  @tparam F callable type
  @param params task parameters
  @param f callable

  <p><!-- Doxygen warning workaround --></p>

  @code{.cpp}
  taskflow.emplace([&](tf::Runtime& rt){
    rt.silent_async("my task", [](){});
    rt.corun_all();
  });
  @endcode
  */
  template <typename P, typename F>
  void silent_async(P&& params, F&& f);
  
  /**
  @brief co-runs the given target and waits until it completes
  
  A corunnable target must have `tf::Graph& T::graph()` defined.

  // co-run a taskflow and wait until all tasks complete
  @code{.cpp}
  tf::Taskflow taskflow1, taskflow2;
  taskflow1.emplace([](){ std::cout << "running taskflow1\n"; });
  taskflow2.emplace([&](tf::Runtime& rt){
    std::cout << "running taskflow2\n";
    rt.corun(taskflow1);
  });
  executor.run(taskflow2).wait();
  @endcode

  Although tf::Runtime::corun blocks until the operation completes, 
  the caller thread (worker) is not blocked (e.g., sleeping or holding any lock).
  Instead, the caller thread joins the work-stealing loop of the executor 
  and returns when all tasks in the target completes.
  
  @attention
  The method is not thread-safe as it modifies the anchor state of the node for exception handling.
  */
  template <typename T>
  void corun(T&& target);

  /**
  @brief corun all asynchronous tasks spawned by this runtime with other workers

  Coruns all asynchronous tasks (tf::Runtime::async,
  tf::Runtime::silent_async) with other workers until all those 
  asynchronous tasks finish.
    
  @code{.cpp}
  std::atomic<size_t> counter{0};
  taskflow.emplace([&](tf::Runtime& rt){
    // spawn 100 async tasks and wait
    for(int i=0; i<100; i++) {
      rt.silent_async([&](){ counter++; });
    }
    rt.corun_all();
    assert(counter == 100);
    
    // spawn another 100 async tasks and wait
    for(int i=0; i<100; i++) {
      rt.silent_async([&](){ counter++; });
    }
    rt.corun_all();
    assert(counter == 200);
  });
  @endcode

  @attention
  The method is not thread-safe as it modifies the anchor state of the node for exception handling.
  */
  inline void corun_all();

  protected:
  
  /**
  @private
  */
  explicit Runtime(Executor&, Worker&, Node*);
  
  /**
  @private
  */
  Executor& _executor;
  
  /**
  @private
  */
  Worker& _worker;
  
  /**
  @private
  */
  Node* _parent;
  
  /**
  @private
  */
  bool _preempted {false};
};

// constructor
inline Runtime::Runtime(Executor& executor, Worker& worker, Node* parent) :
  _executor {executor},
  _worker   {worker},
  _parent   {parent} {
}

// Function: executor
inline Executor& Runtime::executor() {
  return _executor;
}

// Function: worker
inline Worker& Runtime::worker() {
  return _worker;
}

// Procedure: schedule
inline void Runtime::schedule(Task task) {
  
  TF_RUNTIME_CHECK_CALLER("schedule must be called by a worker of runtime's executor");
  
  auto node = task._node;
  // need to keep the invariant: when scheduling a task, the task must have
  // zero dependency (join counter is 0)
  // or we can encounter bug when inserting a nested flow (e.g., module task)
  node->_join_counter.store(0, std::memory_order_relaxed);

  auto& j = node->_parent ? node->_parent->_join_counter :
                            node->_topology->_join_counter;
  j.fetch_add(1, std::memory_order_relaxed);
  _executor._schedule(_worker, node);
}

// Procedure: corun
template <typename T>
void Runtime::corun(T&& target) {

  static_assert(has_graph_v<T>, "target must define a member function 'Graph& graph()'");

  TF_RUNTIME_CHECK_CALLER("corun must be called by a worker of runtime's executor");
  _executor._corun_graph(*pt::this_worker, _parent, target.graph().begin(), target.graph().end());
}

// Function: corun_all
inline void Runtime::corun_all() {
  TF_RUNTIME_CHECK_CALLER("corun_all must be called by a worker of runtime's executor");
  {
    AnchorGuard anchor(_parent);
    _executor._corun_until(_worker, [this] () -> bool {
      return _parent->_join_counter.load(std::memory_order_acquire) == 0;
    });
  }
  _parent->_rethrow_exception();
}

// ------------------------------------
// Runtime::silent_async series
// ------------------------------------

// Function: silent_async
template <typename F>
void Runtime::silent_async(F&& f) {
  silent_async(DefaultTaskParams{}, std::forward<F>(f));
}

// Function: silent_async
template <typename P, typename F>
void Runtime::silent_async(P&& params, F&& f) {
  _parent->_join_counter.fetch_add(1, std::memory_order_relaxed);
  _executor._silent_async(
    std::forward<P>(params), std::forward<F>(f), _parent->_topology, _parent
  );
}

// ------------------------------------
// Runtime::async series
// ------------------------------------

// Function: async
template <typename F>
auto Runtime::async(F&& f) {
  return async(DefaultTaskParams{}, std::forward<F>(f));
}

// Function: async
template <typename P, typename F>
auto Runtime::async(P&& params, F&& f) {
  _parent->_join_counter.fetch_add(1, std::memory_order_relaxed);
  return _executor._async(
    std::forward<P>(params), std::forward<F>(f), _parent->_topology, _parent
  );
}

// ----------------------------------------------------------------------------
// Preemption guard
// ----------------------------------------------------------------------------

/**
@private
*/
class PreemptionGuard {

  public:

  PreemptionGuard(Runtime& runtime) : _runtime {runtime} {
    if(_runtime._preempted == true) {
      TF_THROW("runtime is not preemptible");
    }
    _runtime._parent->_nstate |= NSTATE::PREEMPTED;
    _runtime._preempted = true;
    _runtime._parent->_join_counter.fetch_add(1, std::memory_order_release);
  }

  ~PreemptionGuard() {
    if(_runtime._parent->_join_counter.fetch_sub(1, std::memory_order_acq_rel) == 1) {
      _runtime._preempted = false;
      _runtime._parent->_nstate &= ~NSTATE::PREEMPTED;
    }
  }

  PreemptionGuard(const PreemptionGuard&) = delete;
  PreemptionGuard(PreemptionGuard&&) = delete;

  PreemptionGuard& operator = (const PreemptionGuard&) = delete;
  PreemptionGuard& operator = (PreemptionGuard&&) = delete;
  
  private:

  Runtime& _runtime;
};


// ----------------------------------------------------------------------------
// Executor Forward Declaration
// ----------------------------------------------------------------------------

// Procedure: _invoke_runtime_task
inline bool Executor::_invoke_runtime_task(Worker& worker, Node* node) {
  return _invoke_runtime_task_impl(
    worker, node, std::get_if<Node::Runtime>(&node->_handle)->work
  );
}

// Function: _invoke_runtime_task_impl
inline bool Executor::_invoke_runtime_task_impl(
  Worker& worker, Node* node, std::function<void(Runtime&)>& work
) {
  // first time
  if((node->_nstate & NSTATE::PREEMPTED) == 0) {

    Runtime rt(*this, worker, node);

    _observer_prologue(worker, node);
    TF_EXECUTOR_EXCEPTION_HANDLER(worker, node, {
      work(rt);
    });
    _observer_epilogue(worker, node);
    
    // here, we cannot check the state from node->_nstate due to data race
    if(rt._preempted) {
      return true;
    }
  }
  // second time - previously preempted
  else {
    node->_nstate &= ~NSTATE::PREEMPTED;
  }
  return false;
}

// Function: _invoke_runtime_task_impl
inline bool Executor::_invoke_runtime_task_impl(
  Worker& worker, Node* node, std::function<void(Runtime&, bool)>& work
) {
    
  Runtime rt(*this, worker, node);

  // first time
  if((node->_nstate & NSTATE::PREEMPTED) == 0) {

    _observer_prologue(worker, node);
    TF_EXECUTOR_EXCEPTION_HANDLER(worker, node, {
      work(rt, false);
    });
    _observer_epilogue(worker, node);
    
    // here, we cannot check the state from node->_nstate due to data race
    if(rt._preempted) {
      return true;
    }
  }
  // second time - previously preempted
  else {
    node->_nstate &= ~NSTATE::PREEMPTED;
  }

  // clean up outstanding work
  work(rt, true);

  return false;
}


// ----------------------------------------------------------------------------
// Executor Members that Depend on Runtime
// ----------------------------------------------------------------------------

template <typename P, typename F>
auto Executor::_async(P&& params, F&& f, Topology* tpg, Node* parent) {
  
  // async task with runtime: [] (tf::Runtime&) { ... }
  if constexpr (is_runtime_task_v<F>) {

    std::promise<void> p;
    auto fu{p.get_future()};
    
    _schedule_async_task(animate(
      NSTATE::NONE, ESTATE::ANCHORED, std::forward<P>(params), tpg, parent, 0, 
      std::in_place_type_t<Node::Async>{}, 
      [p=MoC{std::move(p)}, f=std::forward<F>(f)](Runtime& rt, bool reentered) mutable { 
        if(!reentered) {
          f(rt);
        }
        else {
          auto& eptr = rt._parent->_exception_ptr;
          eptr ? p.object.set_exception(eptr) : p.object.set_value();
        }
      }
    ));
    return fu;
  }
  // async task with closure: [] () { ... }
  else if constexpr (std::is_invocable_v<F>){
    std::packaged_task p(std::forward<F>(f));
    auto fu{p.get_future()};
    _schedule_async_task(animate(
      std::forward<P>(params), tpg, parent, 0, 
      std::in_place_type_t<Node::Async>{}, 
      [p=make_moc(std::move(p))]() mutable { p.object(); }
    ));
    return fu;
  }
  else {
    static_assert(dependent_false_v<F>, 
      "invalid async target - must be one of the following types:\n\
      (1) [] (tf::Runtime&) -> void {}\n\
      (2) [] () -> auto { ... return ... }\n"
    );
  }

}

// Function: _silent_async
template <typename P, typename F>
void Executor::_silent_async(P&& params, F&& f, Topology* tpg, Node* parent) {
  // silent task 
  if constexpr (is_runtime_task_v<F> || std::is_invocable_v<F>) {
    _schedule_async_task(animate(
      std::forward<P>(params), tpg, parent, 0,
      std::in_place_type_t<Node::Async>{}, std::forward<F>(f)
    ));
  }
  // invalid silent async target
  else {
    static_assert(dependent_false_v<F>, 
      "invalid silent_async target - must be one of the following types:\n\
      (1) [] (tf::Runtime&) -> void {}\n\
      (2) [] () -> auto { ... return ... }\n"
    );
  }
}


}  // end of namespace tf -----------------------------------------------------









