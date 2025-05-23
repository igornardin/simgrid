/* Copyright (c) 2006-2025. The SimGrid Team. All rights reserved.          */

/* This program is free software; you can redistribute it and/or modify it
 * under the terms of the license (GNU LGPL) which comes with this package. */

#ifndef SIMGRID_S4U_MAILBOX_HPP
#define SIMGRID_S4U_MAILBOX_HPP

#include <simgrid/forward.h>

#include <simgrid/Exception.hpp>
#include <simgrid/s4u/Actor.hpp>
#include <simgrid/s4u/Comm.hpp>
#include <smpi/forward.hpp>

#include <memory>
#include <string>

namespace simgrid::s4u {

/** @brief Mailboxes: Network rendez-vous points. */
class XBT_PUBLIC Mailbox {
#ifndef DOXYGEN
  friend Comm;
  friend smpi::Request;
  friend kernel::activity::MailboxImpl;
#endif

  kernel::activity::MailboxImpl* const pimpl_;

  explicit Mailbox(kernel::activity::MailboxImpl * mbox) : pimpl_(mbox) {}
  ~Mailbox() = default;

protected:
  kernel::activity::MailboxImpl* get_impl() const { return pimpl_; }

public:
  /** @brief Retrieves the name of that mailbox as a C++ string */
  const std::string& get_name() const;
  /** @brief Retrieves the name of that mailbox as a C string */
  const char* get_cname() const;

  /** \static Retrieve the mailbox associated to the given name. Mailboxes are created on demand. */
  static Mailbox* by_name(const std::string& name);

  /** Returns whether the mailbox contains queued communications */
  bool empty() const;

  /* Returns the number of queued communications */
  size_t size() const;

  /** Check if there is a communication going on in a mailbox. */
  bool listen() const;

  /** Look if there is a communication going on in a mailbox and return the PID of the sender actor */
  aid_t listen_from() const;

  /** Check if there is a communication ready to be consumed from a mailbox.
   * \beginrst
   *  See :ref:`this example <s4u_ex_mailbox_ready>`.
   * \endrst
   */
  bool ready() const;

  /** Gets the first element in the queue (without dequeuing it), or nullptr if none is there */
  kernel::activity::CommImplPtr front() const;

  /** Declare that the specified actor is a permanent receiver on that mailbox
   *
   * It means that the communications sent to this mailbox will start flowing to
   * its host even before it does a get(). This models the real behavior of TCP
   * and MPI communications, amongst other. It will improve the accuracy of
   * predictions, in particular if your application exhibits swarms of small messages.
   *
   * SimGrid does not enforces any kind of ownership over the mailbox. Even if a receiver
   * was declared, any other actors can still get() data from the mailbox. The timings
   * will then probably be off tracks, so you should strive on your side to not get data
   * from someone else's mailbox.
   *
   * Note that being permanent receivers of a mailbox prevents actors to be garbage-collected.
   * If your simulation creates many short-lived actors that marked as permanent receiver, you
   * should call mailbox->set_receiver(nullptr) by the end of the actors so that their memory gets
   * properly reclaimed. This call should be at the end of the actor's function, not in an on_exit
   * callback.
   */
  void set_receiver(ActorPtr actor);

  /** Return the actor declared as permanent receiver, or nullptr if none **/
  ActorPtr get_receiver() const;

  /** Creates (but don't start) a data transmission to that mailbox */
  CommPtr put_init();
  /** Creates (but don't start) a data transmission to that mailbox.
   *
   * Please note that if you send a pointer to some data, you must ensure that your data remains live during the
   * communication, or the receiver will get a pointer to a garbled memory area.
   */
  CommPtr put_init(void* data, uint64_t simulated_size_in_bytes);
  /** Creates and start a data transmission to that mailbox.
   *
   * Please note that if you send a pointer to some data, you must ensure that your data remains live during the
   * communication, or the receiver will get a pointer to a garbled memory area.
   */
  CommPtr put_async(void* data, uint64_t simulated_size_in_bytes);

  XBT_DECLARE_ENUM_CLASS(IprobeKind, SEND, RECV);
  kernel::activity::ActivityImplPtr
  iprobe(IprobeKind kind, const std::function<bool(void*, void*, kernel::activity::CommImpl*)>& match_fun, void* data);
  /** Blocking data transmission.
   *
   * Please note that if you send a pointer to some data, you must ensure that your data remains live during the
   * communication, or the receiver will get a pointer to a garbled memory area.
   */
  void put(void* payload, uint64_t simulated_size_in_bytes);
  /** Blocking data transmission with timeout */
  void put(void* payload, uint64_t simulated_size_in_bytes, double timeout);

  /** Creates (but don't start) a data reception onto that mailbox.
   * @verbatim embed:rst:inline You probably want to use :cpp:func:`simgrid::s4u::Comm::set_dst_data` and friends before
   * starting that activity. @endverbatim  */
  CommPtr get_init();
  /** Creates and start an async data reception to that mailbox */
  template <typename T> CommPtr get_async(T** data);
  /** Creates and start an async data reception to that mailbox. Since the data location is not provided, you'll have to
   * use Comm::get_payload once the comm terminates */
  CommPtr get_async();

  /** Blocking data reception */
  template <typename T> T* get();
  template <typename T> std::unique_ptr<T> get_unique() { return std::unique_ptr<T>(get<T>()); }

  /** Blocking data reception with timeout */
  template <typename T> T* get(double timeout);
  template <typename T> std::unique_ptr<T> get_unique(double timeout) { return std::unique_ptr<T>(get<T>(timeout)); }

  void clear();
};

template <typename T> CommPtr Mailbox::get_async(T** data)
{
  CommPtr res = get_init()->set_dst_data(reinterpret_cast<void**>(data), sizeof(void*));
  res->start();
  return res;
}

template <typename T> T* Mailbox::get()
{
  T* res = nullptr;

  CommPtr comm = get_async<T>(&res);
  try {
    comm->wait();
  } catch (simgrid::TimeoutException&) {
    comm->cancel();
    /* Rethrowing the original exception segfaults in parallel tests */
    throw TimeoutException(XBT_THROW_POINT, "Timeouted");
  }

  return res;
}

template <typename T> T* Mailbox::get(double timeout)
{
  T* res = nullptr;
  get_async<T>(&res)->wait_for_or_cancel(timeout);
  return res;
}
} // namespace simgrid::s4u

#endif /* SIMGRID_S4U_MAILBOX_HPP */
