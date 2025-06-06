/* Copyright (c) 2007-2025. The SimGrid Team. All rights reserved.          */

/* This program is free software; you can redistribute it and/or modify it
 * under the terms of the license (GNU LGPL) which comes with this package. */

#include "simgrid/actor.h"
#include "simgrid/engine.h"
#include "simgrid/host.h"
#include "simgrid/zone.h"

#include "xbt/asserts.h"
#include "xbt/dict.h"
#include "xbt/log.h"

XBT_LOG_NEW_DEFAULT_CATEGORY(test, "Property test");

static void test_host(const char* hostname)
{
  sg_host_t thehost   = sg_host_by_name(hostname);
  const char* noexist = "Unknown";
  const char* value;
  char exist[] = "Hdd";

  XBT_INFO("== Print the properties of the host '%s'", hostname);
  int propcount;
  const char** propnames = sg_host_get_property_names(thehost, &propcount);
  for (int cpt = 0; cpt < propcount; cpt++)
    XBT_INFO("  Host property: '%s' -> '%s'", propnames[cpt],
             (char*)sg_host_get_property_value(thehost, propnames[cpt]));
  free(propnames);

  XBT_INFO("== Try to get a host property that does not exist");
  value = sg_host_get_property_value(thehost, noexist);
  xbt_assert(!value, "The key exists (it's not supposed to)");

  XBT_INFO("== Try to get a host property that does exist");
  value = sg_host_get_property_value(thehost, exist);
  xbt_assert(value, "\tProperty %s is undefined (where it should)", exist);
  xbt_assert(!strcmp(value, "180"), "\tValue of property %s is defined to %s (where it should be 180)", exist, value);
  XBT_INFO("   Property: %s old value: %s", exist, value);

  XBT_INFO("== Trying to modify a host property");
  sg_host_set_property_value(thehost, exist, (char*)"250");

  /* Test if we have changed the value */
  value = sg_host_get_property_value(thehost, exist);
  xbt_assert(value, "Property %s is undefined (where it should)", exist);
  xbt_assert(!strcmp(value, "250"), "Value of property %s is defined to %s (where it should be 250)", exist, value);
  XBT_INFO("   Property: %s old value: %s", exist, value);

  /* Restore the value for the next test */
  sg_host_set_property_value(thehost, exist, (char*)"180");
}

static void alice(int argc, char* argv[])
{ /* Dump what we have on the current host */
  test_host("host1");
}

static void carole(int argc, char* argv[])
{                        /* Dump what we have on a remote host */
  sg_actor_sleep_for(1); // Wait for alice to be done with its experiment
  test_host("host1");
}

static void david(int argc, char* argv[])
{                        /* Dump what we have on a remote host */
  sg_actor_sleep_for(2); // Wait for alice and carole to be done with its experiment
  test_host("node-0.simgrid.org");
}

static void bob(int argc, char* argv[])
{
  /* this host also tests the properties of the NetZone*/
  const_sg_netzone_t root = sg_zone_get_by_name("AS0");
  XBT_INFO("== Print the properties of the NetZone");
  XBT_INFO("   Actor property: filename -> %s", sg_zone_get_property_value(root, "filename"));
  XBT_INFO("   Actor property: date -> %s", sg_zone_get_property_value(root, "date"));
  XBT_INFO("   Actor property: author -> %s", sg_zone_get_property_value(root, "author"));

  /* Get the property list of current bob actor */
  const char** propnames = sg_actor_get_property_names(sg_actor_self(), NULL);
  XBT_INFO("== Print the properties of the actor");
  for (int cpt = 0; propnames[cpt] != NULL; cpt++)
    XBT_INFO("   Actor property: %s -> %s", propnames[cpt],
             (char*)sg_actor_get_property_value(sg_actor_self(), propnames[cpt]));
  free(propnames);

  XBT_INFO("== Try to get an actor property that does not exist");
  const char* value = sg_actor_get_property_value(sg_actor_self(), "UnknownActorProp");
  xbt_assert(!value, "The property is defined (it shouldn't)");
}

int main(int argc, char* argv[])
{
  simgrid_init(&argc, argv);
  xbt_assert(argc > 2,
             "Usage: %s platform_file deployment_file\n"
             "\tExample: %s platform.xml deployment.xml\n",
             argv[0], argv[0]);

  simgrid_register_function("alice", alice);
  simgrid_register_function("bob", bob);
  simgrid_register_function("carole", carole);
  simgrid_register_function("david", david);

  simgrid_load_platform(argv[1]);
  size_t host_count = sg_host_count();
  XBT_INFO("There are %zu hosts in the environment", host_count);

  sg_host_t* hosts = sg_host_list();
  for (size_t i = 0; i < host_count; i++)
    XBT_INFO("Host '%s' runs at %.0f flops/s", sg_host_get_name(hosts[i]), sg_host_get_speed(hosts[i]));

  free(hosts);

  simgrid_load_deployment(argv[2]);

  simgrid_run();

  return 0;
}
