/* Copyright (c) 2010-2025. The SimGrid Team. All rights reserved.          */

/* This program is free software; you can redistribute it and/or modify it
 * under the terms of the license (GNU LGPL) which comes with this package. */

#include <simgrid/Exception.hpp>
#include <simgrid/plugins/energy.h>
#include <simgrid/s4u/Engine.hpp>
#include <simgrid/s4u/Exec.hpp>
#include <simgrid/s4u/Host.hpp>
#include <simgrid/s4u/VirtualMachine.hpp>
#include <simgrid/simcall.hpp>

#include "src/kernel/activity/ActivityImpl.hpp"
#include "src/kernel/resource/CpuImpl.hpp"
#include "src/simgrid/module.hpp"

#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>

SIMGRID_REGISTER_PLUGIN(host_energy, "Cpu energy consumption.", &sg_host_energy_plugin_init)

/** @defgroup plugin_host_energy plugin_host_energy Plugin Host Energy

  @beginrst

This is the energy plugin, enabling to account not only for computation time, but also for the dissipated energy in the
simulated platform.
To activate this plugin, first call :cpp:func:`sg_host_energy_plugin_init()` before your loading your platform, and
then use :cpp:func:`sg_host_get_consumed_energy()` to retrieve the consumption of a given host.

When the host is on, this energy consumption naturally depends on both the current CPU load and the host energy profile.
According to our measurements, the consumption is somehow linear in the amount of cores at full speed, with an
abnormality when all the cores are idle. The full details are in `our scientific paper
<https://hal.inria.fr/hal-01523608>`_ on that topic.

As a result, our energy model takes 4 parameters:

  - ``Idle`` wattage (i.e., instantaneous consumption in Watt) when your host is up and running, but without anything to
    do.
  - ``Epsilon`` wattage when all cores are at 0 or epsilon%, but not in Idle state.
  - ``AllCores`` wattage when all cores of the host are at 100%.
  - ``Off`` wattage when the host is turned off.

Here is an example of XML declaration:

.. code-block:: xml

   <host id="HostA" speed="100.0Mf" core="4">
       <prop id="wattage_per_state" value="100.0:120.0:200.0" />
       <prop id="wattage_off" value="10" />
   </host>

If only two values are given, ``Idle`` is used for the missing ``Epsilon`` value.

This example gives the following parameters: ``Off`` is 10 Watts; ``Idle`` is 100 Watts; ``Epsilon`` is 120 Watts and
``AllCores`` is 200 Watts.
This is enough to compute the wattage as a function of the amount of loaded cores:

.. raw:: html

   <table border="1">
   <tr><th>#Cores loaded</th><th>Wattage</th><th>Explanation</th></tr>
   <tr><td>0 (idle)</td><td> 100 Watts&nbsp;</td><td>Idle value</td></tr>
   <tr><td>1</td><td> 140 Watts</td><td> Linear extrapolation between Epsilon and AllCores</td></tr>
   <tr><td>2</td><td> 160 Watts</td><td> Linear extrapolation between Epsilon and AllCores</td></tr>
   <tr><td>3</td><td> 180 Watts</td><td> Linear extrapolation between Epsilon and AllCores</td></tr>
   <tr><td>4</td><td> 200 Watts</td><td> AllCores value</td></tr>
   </table>

Here is how it looks graphically:

.. image:: img/plugin-energy.svg
   :scale: 80%
   :align: center

As you can see, the ``Epsilon`` parameter allows to freely specify the slope you want, while using the 2 parameters
version of the model (with only ``Idle`` and ``AllCores``) requires that the ``Idle`` value is on the extension of the
line crossing the consumption you mesure for each core amount. Please note that specifying the consumption for each core
amount separately was not a solution because parallel tasks can use an amount of cores that is not an integer. The good
news is that it was not necessary, as our experiments (detailed in the paper) show that the proposed linear model is
sufficient to capture reality.

.. raw:: html

   <h4>How does DVFS interact with the host energy model?</h4>

If your host has several DVFS levels (several pstates), then you should give the energetic profile of each pstate level:

.. code-block:: xml

   <host id="HostC" speed="100.0Mf,50.0Mf,20.0Mf" core="4">
       <prop id="wattage_per_state"
             value="95.0:120.0:200.0, 93.0:115.0:170.0, 90.0:110.0:150.0" />
       <prop id="wattage_off" value="10" />
   </host>

This encodes the following values:

.. raw:: html

   <table border="1">
   <tr><th>pstate</th><th>Performance</th><th>Idle</th><th>Epsilon</th><th>AllCores</th></tr>
   <tr><td>0</td><td>100 Mflop/s</td><td>95 Watts</td><td>120 Watts</td><td>200 Watts</td></tr>
   <tr><td>1</td><td>50 Mflop/s</td><td>93 Watts</td><td>115 Watts</td><td>170 Watts</td></tr>
   <tr><td>2</td><td>20 Mflop/s</td><td>90 Watts</td><td>110 Watts</td><td>150 Watts</td></tr>
   </table>

To change the pstate of a given CPU, use the following functions:
:cpp:func:`sg_host_get_nb_pstates()`, :cpp:func:`simgrid::s4u::Host::set_pstate()`,
:cpp:func:`sg_host_get_pstate_speed()`.

.. raw:: html

   <h4>How accurate are these models?</h4>

This model cannot be more accurate than your instantiation: with the default values, your result will not be accurate at
all. You can still get accurate energy prediction, provided that you carefully instantiate the model.
The first step is to ensure that your timing prediction match perfectly. But this is only the first step of the path,
and you really want to read `this paper <https://hal.inria.fr/hal-01523608>`_ to see all what you need to do
before you can get accurate energy predictions.

  @endrst
 */

XBT_LOG_NEW_DEFAULT_SUBCATEGORY(host_energy, kernel, "Logging specific to the host energy plugin");

// Forwards declaration needed to make this function a friend (because friends have external linkage by default)
static void on_simulation_end();

namespace simgrid::plugin {

class PowerRange {
public:
  double idle_;
  double epsilon_;
  double max_;
  double slope_;

  PowerRange(double idle, double epsilon, double max) : idle_(idle), epsilon_(epsilon), max_(max), slope_(max-epsilon) {}
};

class HostEnergy {
  simgrid::s4u::Host* host_ = nullptr;
  /*< List of (idle_power, epsilon_power, max_power) tuple corresponding to each cpu pstate */
  std::vector<PowerRange> power_range_watts_list_;
  bool has_pstate_power_values_ = false; /*< Whether power consumption values were provided for all pstates */

  /* We need to keep track of what pstate has been used, as we will sometimes be notified only *after* a pstate has been
   * used (but we need to update the energy consumption with the old pstate!)
   */
  int pstate_           = 0;
  const int pstate_off_ = -1;
  double watts_off_     = 0.0;              /*< Consumption when the machine is turned off (shutdown) */
  double total_energy_  = 0.0;              /*< Total energy consumed by the host */
  double last_updated_  = simgrid::s4u::Engine::get_clock(); /*< Timestamp of the last energy update event*/

  /* Only used to split total energy into unused/used hosts.
   * If you want to get this info for something else, rather use the host_load plugin
   */
  bool host_was_used_ = false;

  void init_watts_range_list();
  friend void ::on_simulation_end(); // For access to host_was_used_

public:
  static simgrid::xbt::Extension<simgrid::s4u::Host, HostEnergy> EXTENSION_ID;

  explicit HostEnergy(simgrid::s4u::Host* ptr);
  ~HostEnergy();

  bool has_pstate_power_values() const;

  double get_current_watts_value();
  double get_current_watts_value(double cpu_load) const;
  double get_consumed_energy();
  double get_watt_idle_at(int pstate) const;
  double get_watt_min_at(int pstate) const;
  double get_watt_max_at(int pstate) const;
  double get_power_range_slope_at(int pstate) const;
  double get_last_update_time() const { return last_updated_; }
  void update();
};

simgrid::xbt::Extension<simgrid::s4u::Host, HostEnergy> HostEnergy::EXTENSION_ID;

/* Returns whether power consumption values were provided for all pstates. */
bool HostEnergy::has_pstate_power_values() const {
  return has_pstate_power_values_;
}

/* Computes the consumption so far. Called lazily on need. */
void HostEnergy::update()
{
  double start_time  = last_updated_;
  double finish_time = simgrid::s4u::Engine::get_clock();
  //
  // We may have start == finish if the past consumption was updated since the simcall was started
  // for example if 2 actors requested to update the same host's consumption in a given scheduling round.
  //
  // Even in this case, we need to save the pstate for the next call (after this if),
  // which may have changed since that recent update.
  if (start_time < finish_time) {
    double previous_energy = total_energy_;

    double instantaneous_power_consumption = this->get_current_watts_value();

    double energy_this_step = instantaneous_power_consumption * (finish_time - start_time);

    // TODO Trace: Trace energy_this_step from start_time to finish_time in host->getName()

    total_energy_ = previous_energy + energy_this_step;
    last_updated_ = finish_time;

    XBT_DEBUG("[update_energy of %s] period=[%.8f-%.8f]; current speed=%.2E flop/s (pstate %i); total consumption "
              "before: %.8f J -> added now: %.8f J",
              host_->get_cname(), start_time, finish_time, host_->get_pstate_speed(pstate_), pstate_, previous_energy,
              energy_this_step);
  }

  /* Save data for the upcoming time interval: whether it's on/off and the pstate if it's on */
  pstate_ = host_->is_on() ? host_->get_pstate() : pstate_off_;
}

HostEnergy::HostEnergy(simgrid::s4u::Host* ptr) : host_(ptr)
{
  init_watts_range_list();

  const char* off_power_str = host_->get_property("wattage_off");
  if (off_power_str != nullptr) {
    try {
      this->watts_off_ = std::stod(off_power_str);
    } catch (const std::invalid_argument&) {
      throw std::invalid_argument("Invalid value for property wattage_off of host " + host_->get_name() + ": " +
                                  off_power_str);
    }
  }
  /* watts_off is 0 by default */
}

HostEnergy::~HostEnergy() = default;

double HostEnergy::get_watt_idle_at(int pstate) const
{
  if (not has_pstate_power_values_)
    return 0.0;
  return power_range_watts_list_[pstate].idle_;
}

double HostEnergy::get_watt_min_at(int pstate) const
{
  if (not has_pstate_power_values_)
    return 0.0;
  return power_range_watts_list_[pstate].epsilon_;
}

double HostEnergy::get_watt_max_at(int pstate) const
{
  if (not has_pstate_power_values_)
    return 0.0;
  return power_range_watts_list_[pstate].max_;
}

double HostEnergy::get_power_range_slope_at(int pstate) const
{
  if (not has_pstate_power_values_)
    return 0.0;
  return power_range_watts_list_[pstate].slope_;
}

/** @brief Computes the power consumed by the host according to the current situation
 *
 * - If the host is off, that's the watts_off value
 * - if it's on, take the current pstate and the current processor load into account */
double HostEnergy::get_current_watts_value()
{
  if (this->pstate_ == pstate_off_) // The host is off (or was off at the beginning of this time interval)
    return this->watts_off_;

  double current_speed = host_->get_pstate_speed(this->pstate_);

  double cpu_load;

  if (current_speed <= 0)
    // Some users declare a pstate of speed 0 flops (e.g., to model boot time).
    // We consider that the machine is then fully loaded. That's arbitrary but it avoids a NaN
    cpu_load = 1;
  else {
    cpu_load = host_->get_load() / current_speed;

    /* Divide by the number of cores here to have a value between 0 and 1 */
    cpu_load /= host_->get_core_count();

    if (cpu_load > 1) // This condition is true for energy_ptask on 32 bits, even if cpu_load is displayed as 1.000000
      cpu_load = 1;   // That may be an harmless rounding error?
    if (cpu_load > 0)
      host_was_used_ = true;
  }

  return get_current_watts_value(cpu_load);
}

/** @brief Computes the power that the host would consume at the provided processor load
 *
 * Whether the host is ON or OFF is not taken into account.
 */
double HostEnergy::get_current_watts_value(double cpu_load) const
{
  if (not has_pstate_power_values_)
    return 0.0;

  /* Return watts_off if pstate == pstate_off (ie, if the host is off) */
  if (this->pstate_ == pstate_off_) {
    return watts_off_;
  }

  PowerRange power_range = power_range_watts_list_.at(this->pstate_);
  double current_power;

  if (cpu_load > 0)
  {
      /**
       * Something is going on, the host is not idle.
       *
       * The power consumption follows the regular model:
       * P(cpu_load) = Pstatic + Pdynamic * cpu_load
       * where Pstatic = power_range.epsilon_ and Pdynamic = power_range.slope_
       * and the cpu_load is a value between 0 and 1.
       */
      current_power = power_range.epsilon_ + cpu_load * power_range.slope_;
  }
  else
  {
      /* The host is idle, take the dedicated value! */
      current_power = power_range.idle_;
  }

  XBT_DEBUG("[get_current_watts] pstate=%i, epsilon_power=%f, max_power=%f, slope=%f", this->pstate_, power_range.epsilon_,
            power_range.max_, power_range.slope_);
  XBT_DEBUG("[get_current_watts] Current power (watts) = %f, load = %f", current_power, cpu_load);

  return current_power;
}

double HostEnergy::get_consumed_energy()
{
  if (last_updated_ < simgrid::s4u::Engine::get_clock()) // We need to simcall this as it modifies the environment
    simgrid::kernel::actor::simcall_answered(std::bind(&HostEnergy::update, this));

  return total_energy_;
}

void HostEnergy::init_watts_range_list()
{
  const char* all_power_values_str = host_->get_property("wattage_per_state");
  if (all_power_values_str == nullptr) {
    XBT_WARN("No energetic profiles (wattage_per_state) given for host %s, using 0 W by default. Direct request of power/energy consumption of this host will fail.", host_->get_cname());
    return;
  }

  std::vector<std::string> all_power_values;
  boost::split(all_power_values, all_power_values_str, boost::is_any_of(","));
  XBT_DEBUG("%s: power properties: %s", host_->get_cname(), all_power_values_str);

  xbt_assert(all_power_values.size() == host_->get_pstate_count(),
             "Invalid XML file. Found %zu energetic profiles for %lu pstates", all_power_values.size(),
             host_->get_pstate_count());

  int i = 0;
  for (auto const& current_power_values_str : all_power_values) {
    /* retrieve the power values associated with the pstate i */
    std::vector<std::string> current_power_values;
    boost::split(current_power_values, current_power_values_str, boost::is_any_of(":"));

    xbt_assert(current_power_values.size() == 2 || current_power_values.size() == 3,
               "Power properties incorrectly defined for host %s."
               "It should be 'Idle:AllCores' (or 'Idle:Epsilon:AllCores') power values.",
               host_->get_cname());

    double idle_power;
    double epsilon_power;
    double max_power;

    auto msg_idle    = xbt::string_printf("Invalid Idle value for pstate %d on host %s", i, host_->get_cname());
    auto msg_epsilon = xbt::string_printf("Invalid Epsilon value for pstate %d on host %s", i, host_->get_cname());
    auto msg_max     = xbt::string_printf("Invalid AllCores value for pstate %d on host %s", i, host_->get_cname());

    idle_power = xbt_str_parse_double((current_power_values.at(0)).c_str(), msg_idle.c_str());
    if (current_power_values.size() == 2) { // Case: Idle:AllCores
      epsilon_power = xbt_str_parse_double((current_power_values.at(0)).c_str(), msg_idle.c_str());
      max_power     = xbt_str_parse_double((current_power_values.at(1)).c_str(), msg_max.c_str());
    } else { // Case: Idle:Epsilon:AllCores
      epsilon_power = xbt_str_parse_double((current_power_values.at(1)).c_str(), msg_epsilon.c_str());
      max_power     = xbt_str_parse_double((current_power_values.at(2)).c_str(), msg_max.c_str());
    }

    XBT_DEBUG("Creating PowerRange for host %s. Idle:%f, Epsilon:%f, AllCores:%f.", host_->get_cname(), idle_power, epsilon_power, max_power);

    PowerRange range(idle_power, epsilon_power, max_power);
    power_range_watts_list_.push_back(range);
    ++i;
  }

  has_pstate_power_values_ = true;
}
} // namespace simgrid::plugin

using simgrid::plugin::HostEnergy;

/* **************************** events  callback *************************** */
static void on_creation(simgrid::s4u::Host& host)
{
  if (dynamic_cast<simgrid::s4u::VirtualMachine*>(&host)) // Ignore virtual machines
    return;

  // TODO Trace: set to zero the energy variable associated to host->get_name()

  host.extension_set(new HostEnergy(&host));
}

static void on_action_state_change(simgrid::kernel::resource::CpuAction const& action,
                                   simgrid::kernel::resource::Action::State /*previous*/)
{
  for (simgrid::kernel::resource::CpuImpl const* cpu : action.cpus()) {
    simgrid::s4u::Host* host = cpu->get_iface();
    if (host != nullptr) {
      // If it's a VM, take the corresponding PM
      if (const auto* vm = dynamic_cast<simgrid::s4u::VirtualMachine*>(host))
        host = vm->get_pm();

      // Get the host_energy extension for the relevant host
      auto* host_energy = host->extension<HostEnergy>();

      if (host_energy->get_last_update_time() < simgrid::s4u::Engine::get_clock())
        host_energy->update();
    }
  }
}

/* This callback is fired either when the host changes its state (on/off) ("onStateChange") or its speed
 * (because the user changed the pstate, or because of external trace events) ("onSpeedChange") */
static void on_host_change(simgrid::s4u::Host const& h)
{
  const auto* host = &h;
  if (const auto* vm = dynamic_cast<simgrid::s4u::VirtualMachine const*>(host)) // Take the PM of virtual machines
    host = vm->get_pm();

  host->extension<HostEnergy>()->update();
}

static void on_host_destruction(simgrid::s4u::Host const& host)
{
  if (dynamic_cast<simgrid::s4u::VirtualMachine const*>(&host)) // Ignore virtual machines
    return;

  XBT_INFO("Energy consumption of host %s: %f Joules", host.get_cname(),
           host.extension<HostEnergy>()->get_consumed_energy());
}

static void on_simulation_end()
{
  double total_energy      = 0.0; // Total energy consumption (whole platform)
  double used_hosts_energy = 0.0; // Energy consumed by hosts that computed something
  for (simgrid::s4u::Host const* host : simgrid::s4u::Engine::get_instance()->get_all_hosts()) {
    if (host && dynamic_cast<const simgrid::s4u::VirtualMachine*>(host) == nullptr) { // Ignore virtual machines
      double energy = host->extension<HostEnergy>()->get_consumed_energy();
      total_energy += energy;
      if (host->extension<HostEnergy>()->host_was_used_)
        used_hosts_energy += energy;
    }
  }
  XBT_INFO("Total energy consumption: %f Joules (used hosts: %f Joules; unused/idle hosts: %f)", total_energy,
           used_hosts_energy, total_energy - used_hosts_energy);
}

static void on_activity_suspend_resume(simgrid::s4u::Activity const& activity)
{
  if (const auto* action = dynamic_cast<simgrid::kernel::resource::CpuAction*>(activity.get_impl()->model_action_))
    on_action_state_change(*action, /*ignored*/ action->get_state());
}

/* **************************** Public interface *************************** */

/** @ingroup plugin_host_energy
 * @brief Enable host energy plugin
 * @details Enable energy plugin to get joules consumption of each cpu. Call this function before loading your platform.
 */
void sg_host_energy_plugin_init()
{
  if (HostEnergy::EXTENSION_ID.valid())
    return;

  HostEnergy::EXTENSION_ID = simgrid::s4u::Host::extension_create<HostEnergy>();

  simgrid::s4u::Host::on_creation_cb(&on_creation);
  simgrid::s4u::Host::on_onoff_cb(&on_host_change);
  simgrid::s4u::Host::on_speed_change_cb(&on_host_change);
  simgrid::s4u::Host::on_destruction_cb(&on_host_destruction);
  simgrid::s4u::Host::on_exec_state_change_cb(&on_action_state_change);
  simgrid::s4u::VirtualMachine::on_suspend_cb(&on_host_change);
  simgrid::s4u::VirtualMachine::on_resume_cb(&on_host_change);
  simgrid::s4u::Exec::on_suspend_cb(on_activity_suspend_resume);
  simgrid::s4u::Exec::on_resume_cb(on_activity_suspend_resume);
  simgrid::s4u::Engine::on_simulation_end_cb(&on_simulation_end);
  // We may only have one actor on a node. If that actor executes something like
  //   compute -> recv -> compute
  // the recv operation will not trigger a "Host::on_exec_state_change_cb". This means
  // that the next trigger would be the 2nd compute, hence ignoring the idle time
  // during the recv call. By updating at the beginning of a compute, we can
  // fix that. (If the cpu is not idle, this is not required.)
  simgrid::s4u::Exec::on_start_cb([](simgrid::s4u::Exec const& activity) {
    if (activity.get_host_number() == 1) { // We only run on one host
      simgrid::s4u::Host* host = activity.get_host();
      if (const auto* vm = dynamic_cast<simgrid::s4u::VirtualMachine*>(host))
        host = vm->get_pm();
      xbt_assert(host != nullptr);
      host->extension<HostEnergy>()->update();
    }
  });
}

/** @ingroup plugin_host_energy
 *  @brief updates the consumption of all hosts
 *
 * After this call, sg_host_get_consumed_energy() will not interrupt your process
 * (until after the next clock update).
 */
void sg_host_energy_update_all()
{
  simgrid::kernel::actor::simcall_answered([]() {
    std::vector<simgrid::s4u::Host*> list = simgrid::s4u::Engine::get_instance()->get_all_hosts();
    for (auto const& host : list)
      if (dynamic_cast<simgrid::s4u::VirtualMachine*>(host) == nullptr) { // Ignore virtual machines
        xbt_assert(host != nullptr);
        host->extension<HostEnergy>()->update();
      }
  });
}

static void ensure_plugin_inited()
{
  if (not HostEnergy::EXTENSION_ID.valid())
    throw simgrid::xbt::InitializationError("The Energy plugin is not active. Please call sg_host_energy_plugin_init() "
                                            "before calling any function related to that plugin.");
}

/** @ingroup plugin_host_energy
 *  @brief Returns the total energy consumed by the host so far (in Joules)
 *
 *  Please note that since the consumption is lazily updated, it may require a simcall to update it.
 *  The result is that the actor requesting this value will be interrupted,
 *  the value will be updated in kernel mode before returning the control to the requesting actor.
 */
double sg_host_get_consumed_energy(const_sg_host_t host)
{
  ensure_plugin_inited();
  auto* host_energy = host->extension<HostEnergy>();
  xbt_assert(host_energy->has_pstate_power_values(), "No power range properties specified for host %s",
             host->get_cname());
  return host_energy->get_consumed_energy();
}

/** @ingroup plugin_host_energy
 *  @brief Get the amount of watt dissipated when the host is idling
 */
double sg_host_get_idle_consumption(const_sg_host_t host)
{
  ensure_plugin_inited();
  return host->extension<HostEnergy>()->get_watt_idle_at(0);
}

/** @ingroup plugin_host_energy
 *  @brief Get the amount of watt dissipated at the given pstate when the host is idling
 */
double sg_host_get_idle_consumption_at(const_sg_host_t host, int pstate)
{
  ensure_plugin_inited();
  return host->extension<HostEnergy>()->get_watt_idle_at(pstate);
}

/** @ingroup plugin_host_energy
 *  @brief Get the amount of watt dissipated at the given pstate when the host is at 0 or epsilon% CPU usage.
 */
double sg_host_get_wattmin_at(const_sg_host_t host, int pstate)
{
  ensure_plugin_inited();
  return host->extension<HostEnergy>()->get_watt_min_at(pstate);
}
/** @ingroup plugin_host_energy
 *  @brief  Returns the amount of watt dissipated at the given pstate when the host burns CPU at 100%
 */
double sg_host_get_wattmax_at(const_sg_host_t host, int pstate)
{
  ensure_plugin_inited();
  return host->extension<HostEnergy>()->get_watt_max_at(pstate);
}
/** @ingroup plugin_host_energy
 *  @brief  Returns the power slope at the given pstate
 */
double sg_host_get_power_range_slope_at(const_sg_host_t host, int pstate)
{
  ensure_plugin_inited();
  return host->extension<HostEnergy>()->get_power_range_slope_at(pstate);
}
/** @ingroup plugin_host_energy
 *  @brief Returns the current consumption of the host
 */
double sg_host_get_current_consumption(const_sg_host_t host)
{
  ensure_plugin_inited();
  auto* host_energy = host->extension<HostEnergy>();
  xbt_assert(host_energy->has_pstate_power_values(), "No power range properties specified for host %s",
             host->get_cname());
  return host_energy->get_current_watts_value();
}
