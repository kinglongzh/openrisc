/*
 *  Multiprocessing Support for the Region Manager
 *
 *
 *  COPYRIGHT (c) 1989-1999.
 *  On-Line Applications Research Corporation (OAR).
 *
 *  The license and distribution terms for this file may be
 *  found in the file LICENSE in this distribution or at
 *  http://www.OARcorp.com/rtems/license.html.
 *
 *  $Id: regionmp.c,v 1.2 2001-09-27 11:59:19 chris Exp $
 */

#include <rtems/system.h>
#include <rtems/rtems/status.h>
#include <rtems/score/mpci.h>
#include <rtems/score/mppkt.h>
#include <rtems/score/object.h>
#include <rtems/rtems/options.h>
#include <rtems/rtems/region.h>
#include <rtems/score/thread.h>
#include <rtems/rtems/support.h>

/*PAGE
 *
 *  _Region_MP_Send_process_packet
 *
 */

void _Region_MP_Send_process_packet (
  Region_MP_Remote_operations  operation,
  Objects_Id                      region_id,
  rtems_name                      name,
  Objects_Id                      proxy_id
)
{
  Region_MP_Packet *the_packet;
  unsigned32           node;

  switch ( operation ) {

    case REGION_MP_ANNOUNCE_CREATE:
    case REGION_MP_ANNOUNCE_DELETE:
    case REGION_MP_EXTRACT_PROXY:

      the_packet                    = _Region_MP_Get_packet();
      the_packet->Prefix.the_class  = MP_PACKET_REGION;
      the_packet->Prefix.length     = sizeof ( Region_MP_Packet );
      the_packet->Prefix.to_convert = sizeof ( Region_MP_Packet );
      the_packet->operation         = operation;
      the_packet->Prefix.id         = region_id;
      the_packet->name              = name;
      the_packet->proxy_id          = proxy_id;

      if ( operation == REGION_MP_EXTRACT_PROXY )
         node = rtems_get_node( region_id );
      else
         node = MPCI_ALL_NODES;

      _MPCI_Send_process_packet( node, &the_packet->Prefix );
      break;

    case REGION_MP_GET_SEGMENT_REQUEST:
    case REGION_MP_GET_SEGMENT_RESPONSE:
    case REGION_MP_RETURN_SEGMENT_REQUEST:
    case REGION_MP_RETURN_SEGMENT_RESPONSE:
      break;
  }
}

/*PAGE
 *
 *  _Region_MP_Send_request_packet
 *
 */

rtems_status_code _Region_MP_Send_request_packet (
  Region_MP_Remote_operations  operation,
  Objects_Id                   region_id,
  void                        *segment,
  unsigned32                   size,
  rtems_option                 option_set,
  rtems_interval               timeout
)
{
  Region_MP_Packet *the_packet;

  switch ( operation ) {

    case REGION_MP_GET_SEGMENT_REQUEST:
    case REGION_MP_RETURN_SEGMENT_REQUEST:

      the_packet                    = _Region_MP_Get_packet();
      the_packet->Prefix.the_class  = MP_PACKET_REGION;
      the_packet->Prefix.length     = sizeof ( Region_MP_Packet );
      the_packet->Prefix.to_convert = sizeof ( Region_MP_Packet );
      if ( ! _Options_Is_no_wait(option_set))
          the_packet->Prefix.timeout = timeout;

      the_packet->operation         = operation;
      the_packet->Prefix.id         = region_id;
      the_packet->segment           = segment;
      the_packet->size              = size;
      the_packet->option_set        = option_set;

      return (rtems_status_code) _MPCI_Send_request_packet(
          rtems_get_node( region_id ),
          &the_packet->Prefix,
          STATES_READY      /* Not used */
        );
      break;

    case REGION_MP_ANNOUNCE_CREATE:
    case REGION_MP_ANNOUNCE_DELETE:
    case REGION_MP_EXTRACT_PROXY:
    case REGION_MP_GET_SEGMENT_RESPONSE:
    case REGION_MP_RETURN_SEGMENT_RESPONSE:
      break;

  }
  /*
   *  The following line is included to satisfy compilers which
   *  produce warnings when a function does not end with a return.
   */
  return RTEMS_INTERNAL_ERROR;
}

/*PAGE
 *
 *  _Region_MP_Send_response_packet
 *
 */

void _Region_MP_Send_response_packet (
  Region_MP_Remote_operations  operation,
  Objects_Id                   region_id,
  Thread_Control              *the_thread
)
{
  Region_MP_Packet *the_packet;

  switch ( operation ) {

    case REGION_MP_GET_SEGMENT_RESPONSE:
    case REGION_MP_RETURN_SEGMENT_RESPONSE:

      the_packet = ( Region_MP_Packet *) the_thread->receive_packet;

/*
 *  The packet being returned already contains the class, length, and
 *  to_convert fields, therefore they are not set in this routine.
 */
      the_packet->operation = operation;
      the_packet->Prefix.id = the_packet->Prefix.source_tid;

      _MPCI_Send_response_packet(
        rtems_get_node( the_packet->Prefix.source_tid ),
        &the_packet->Prefix
      );
      break;

    case REGION_MP_ANNOUNCE_CREATE:
    case REGION_MP_ANNOUNCE_DELETE:
    case REGION_MP_EXTRACT_PROXY:
    case REGION_MP_GET_SEGMENT_REQUEST:
    case REGION_MP_RETURN_SEGMENT_REQUEST:
      break;

  }
}

/*PAGE
 *
 *
 *  _Region_MP_Process_packet
 *
 */

void _Region_MP_Process_packet (
  rtems_packet_prefix  *the_packet_prefix
)
{
  Region_MP_Packet *the_packet;
  Thread_Control   *the_thread;
  boolean           ignored;

  the_packet = (Region_MP_Packet *) the_packet_prefix;

  switch ( the_packet->operation ) {

    case REGION_MP_ANNOUNCE_CREATE:

      ignored = _Objects_MP_Allocate_and_open(
                  &_Region_Information,
                  the_packet->name,
                  the_packet->Prefix.id,
                  TRUE
                );

      _MPCI_Return_packet( the_packet_prefix );
      break;

    case REGION_MP_ANNOUNCE_DELETE:

      _Objects_MP_Close( &_Region_Information, the_packet->Prefix.id );

      _MPCI_Return_packet( the_packet_prefix );
      break;

    case REGION_MP_EXTRACT_PROXY:

      the_thread = _Thread_MP_Find_proxy( the_packet->proxy_id );

      if ( ! _Thread_Is_null( the_thread ) )
        _Thread_queue_Extract( the_thread->Wait.queue, the_thread );

      _MPCI_Return_packet( the_packet_prefix );
      break;

    case REGION_MP_GET_SEGMENT_REQUEST:

      the_packet->Prefix.return_code = rtems_region_get_segment(
        the_packet->Prefix.id,
        the_packet->size,
        the_packet->option_set,
        the_packet->Prefix.timeout,
        &the_packet->segment
      );

      _Region_MP_Send_response_packet(
        REGION_MP_GET_SEGMENT_RESPONSE,
        the_packet->Prefix.id,
        _Thread_Executing
      );
      break;

    case REGION_MP_GET_SEGMENT_RESPONSE:

      the_thread = _MPCI_Process_response( the_packet_prefix );

      *(void **)the_thread->Wait.return_argument = the_packet->segment;

      _MPCI_Return_packet( the_packet_prefix );
      break;

    case REGION_MP_RETURN_SEGMENT_REQUEST:

      the_packet->Prefix.return_code = rtems_region_return_segment(
        the_packet->Prefix.id,
        the_packet->segment
      );

      _Region_MP_Send_response_packet(
        REGION_MP_RETURN_SEGMENT_RESPONSE,
        the_packet->Prefix.id,
        _Thread_Executing
      );
      break;

    case REGION_MP_RETURN_SEGMENT_RESPONSE:

      the_thread = _MPCI_Process_response( the_packet_prefix );

      _MPCI_Return_packet( the_packet_prefix );
      break;

  }
}

/*PAGE
 *
 *  _Region_MP_Send_object_was_deleted
 *
 *  This routine is not needed by the Region since a region
 *  cannot be deleted when segments are in use.
 *
 */

/*PAGE
 *
 *  _Region_MP_Send_extract_proxy
 *
 */

void _Region_MP_Send_extract_proxy (
  Thread_Control *the_thread
)
{
  _Region_MP_Send_process_packet(
    REGION_MP_EXTRACT_PROXY,
    the_thread->Wait.id,
    (rtems_name) 0,
    the_thread->Object.id
  );
}

/*PAGE
 *
 *  _Region_MP_Get_packet
 *
 */

Region_MP_Packet *_Region_MP_Get_packet ( void )
{
  return ( (Region_MP_Packet *) _MPCI_Get_packet() );
}

/* end of file */
