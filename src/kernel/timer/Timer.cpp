/* Copyright (c) 2021-2025. The SimGrid Team. All rights reserved.          */

/* This program is free software; you can redistribute it and/or modify it
 * under the terms of the license (GNU LGPL) which comes with this package. */

#include <simgrid/kernel/Timer.hpp>
#include <simgrid/s4u/Engine.hpp>

namespace simgrid::kernel::timer {

Timer* Timer::set(double date, xbt::Task<void()>&& callback)
{
  auto* timer    = new Timer(date, std::move(callback));
  timer->handle_ = kernel_timers().emplace(std::make_pair(date, timer));
  return timer;
}

/** @brief cancels a timer that was added earlier */
void Timer::remove()
{
  kernel_timers().erase(handle_);
  delete this;
}

/** Handle any pending timer. Returns if something was actually run. */
bool Timer::execute_all()
{
  bool result = false;
  while (not kernel_timers().empty() && s4u::Engine::get_clock() >= kernel_timers().top().first) {
    result = true;
    Timer* timer = kernel_timers().top().second;
    kernel_timers().pop();
    timer->callback();
    delete timer;
  }
  return result;
}

} // namespace simgrid::kernel::timer
