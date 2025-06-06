/* Copyright (c) 2013-2025. The SimGrid Team. All rights reserved.          */

/* This program is free software; you can redistribute it and/or modify it
 * under the terms of the license (GNU LGPL) which comes with this package. */

/*
 * implemented by Pitch Patarasuk, 07/01/2007
 */
#include "../colls_private.hpp"
//#include <star-reduction.c>


/*
This function performs all-reduce operation as follow.
1) binomial_tree reduce inside each SMP node
2) reduce-scatter -inter between root of each SMP node
3) allgather - inter between root of each SMP node
4) binomial_tree bcast inside each SMP node
*/
namespace simgrid::smpi {
int allreduce__smp_rsag_rab(const void *sbuf, void *rbuf, int count,
                            MPI_Datatype dtype, MPI_Op op,
                            MPI_Comm comm)
{
  int comm_size, rank;
  int tag = COLL_TAG_ALLREDUCE;
  int mask;
  MPI_Status status;
  if(comm->get_leaders_comm()==MPI_COMM_NULL){
    comm->init_smp();
  }
  int num_core=1;
  if (comm->is_uniform()){
    num_core = comm->get_intra_comm()->size();
  }

  comm_size = comm->size();

  if((comm_size&(comm_size-1)))
    throw std::invalid_argument(
        "allreduce smp rsag rab algorithm can't be used with non power of two number of processes!");

  rank = comm->rank();
  MPI_Aint extent;
  extent = dtype->get_extent();
  unsigned char* tmp_buf = smpi_get_tmp_sendbuffer(count * extent);

  int intra_rank, inter_rank;
  intra_rank = rank % num_core;
  inter_rank = rank / num_core;

  int inter_comm_size = (comm_size + num_core - 1) / num_core;

  Request::sendrecv(sbuf, count, dtype, rank, tag,
               rbuf, count, dtype, rank, tag, comm, &status);

  // SMP_binomial_reduce
  mask = 1;
  while (mask < num_core) {
    if ((mask & intra_rank) == 0) {
      int src = (inter_rank * num_core) + (intra_rank | mask);
      //      if (src < ((inter_rank + 1) * num_core)) {
      if (src < comm_size) {
        Request::recv(tmp_buf, count, dtype, src, tag, comm, &status);
        if(op!=MPI_OP_NULL) op->apply( tmp_buf, rbuf, &count, dtype);
      }
    } else {

      int dst = (inter_rank * num_core) + (intra_rank & (~mask));
      Request::send(rbuf, count, dtype, dst, tag, comm);
      break;
    }
    mask <<= 1;
  }


  // INTER: reduce-scatter
  if (intra_rank == 0) {

    int base_offset, send_base_offset, recv_base_offset, recv_chunk;
    int curr_count, i, recv_offset, send_offset;

    // reduce-scatter

    recv_chunk = extent * count / (comm_size / num_core);

    mask = 1;
    curr_count = count / 2;
    base_offset = 0;

    while (mask < (comm_size / num_core)) {
      int dst = inter_rank ^ mask;

      // compute offsets
      // right-handside
      if (inter_rank & mask) {
        recv_base_offset = base_offset + curr_count;
        send_base_offset = base_offset;
        base_offset = recv_base_offset;
      }
      // left-handside
      else {
        recv_base_offset = base_offset;
        send_base_offset = base_offset + curr_count;
      }
      send_offset = send_base_offset * extent;
      recv_offset = recv_base_offset * extent;

      Request::sendrecv((char *)rbuf + send_offset, curr_count, dtype, (dst * num_core), tag,
                   tmp_buf, curr_count, dtype, (dst * num_core), tag,
                   comm, &status);

      if(op!=MPI_OP_NULL) op->apply( tmp_buf, (char *)rbuf + recv_offset, &curr_count, dtype);

      mask *= 2;
      curr_count /= 2;
    }


    // INTER: allgather

    int size = (comm_size / num_core) / 2;
    base_offset = 0;
    mask = 1;
    while (mask < (comm_size / num_core)) {
      if (inter_rank & mask) {
        base_offset += size;
      }
      mask <<= 1;
      size /= 2;
    }

    curr_count *= 2;
    mask >>= 1;
    i = 1;
    while (mask >= 1) {
      // destination pair for both send and recv
      int dst = inter_rank ^ mask;

      // compute offsets
      send_base_offset = base_offset;
      if (inter_rank & mask) {
        recv_base_offset = base_offset - i;
        base_offset -= i;
      } else {
        recv_base_offset = base_offset + i;
      }
      send_offset = send_base_offset * recv_chunk;
      recv_offset = recv_base_offset * recv_chunk;

      Request::sendrecv((char *)rbuf + send_offset, curr_count, dtype, (dst * num_core), tag,
                   (char *)rbuf + recv_offset, curr_count, dtype, (dst * num_core), tag,
                   comm, &status);


      curr_count *= 2;
      i *= 2;
      mask >>= 1;
    }


  }                             // INTER

  // intra SMP binomial bcast

  int num_core_in_current_smp = num_core;
  if (inter_rank == (inter_comm_size - 1)) {
    num_core_in_current_smp = comm_size - (inter_rank * num_core);
  }
  mask = 1;
  while (mask < num_core_in_current_smp) {
    if (intra_rank & mask) {
      int src = (inter_rank * num_core) + (intra_rank - mask);
      Request::recv(rbuf, count, dtype, src, tag, comm, &status);
      break;
    }
    mask <<= 1;
  }

  mask >>= 1;

  while (mask > 0) {
    int dst = (inter_rank * num_core) + (intra_rank + mask);
    if (dst < comm_size) {
      Request::send(rbuf, count, dtype, dst, tag, comm);
    }
    mask >>= 1;
  }


  smpi_free_tmp_buffer(tmp_buf);
  return MPI_SUCCESS;
}
} // namespace simgrid::smpi
