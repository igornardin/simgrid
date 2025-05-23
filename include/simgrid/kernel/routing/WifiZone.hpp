/* Copyright (c) 2013-2025. The SimGrid Team. All rights reserved.          */

/* This program is free software; you can redistribute it and/or modify it
 * under the terms of the license (GNU LGPL) which comes with this package. */

#ifndef SIMGRID_ROUTING_WIFI_HPP_
#define SIMGRID_ROUTING_WIFI_HPP_

#include <simgrid/kernel/routing/RoutedZone.hpp>

namespace simgrid::kernel::routing {

/** @ingroup ROUTING_API
 *  @brief NetZone modeling a Wifi zone
 *
 *  This routing has only one link, representing the wifi medium (ie, the air).
 *  That link is used for all communications within the zone.
 */
class XBT_PRIVATE WifiZone : public RoutedZone {
  resource::StandardLinkImpl* wifi_link_ = nullptr; // Representing the air media (there is no such thing in NS-3)
  NetPoint* access_point_        = nullptr; // Zone's gateway to the external world

  void do_seal() override;
  resource::StandardLinkImpl* do_create_link(const std::string& name, const std::vector<double>& bandwidths) override;

public:
  using RoutedZone::RoutedZone;
  WifiZone(const WifiZone&) = delete;
  WifiZone& operator=(const WifiZone) = delete;

  void get_local_route(const NetPoint* src, const NetPoint* dst, Route* into, double* latency) override;
  NetPoint* get_access_point() const { return access_point_; }
};
} // namespace simgrid::kernel::routing

#endif /* SIMGRID_ROUTING_WIFI_HPP_ */
