//Copyright 2015 <>< Charles Lohr under the ColorChord License.

#include "outdrivers.h"
#include "notefinder.h"
#include <stdio.h>
#include "parameters.h"
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include "color.h"
#include "DrawFunctions.h"
#include <unistd.h>
#include "e131.h"

#if defined(WIN32) || defined(WINDOWS)
#include <windows.h>
#ifdef TCC
#include <winsock2.h>
#endif
#define MSG_NOSIGNAL 0
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#endif

#define MAX_BUFFER 510

struct DPODriver
{
  int leds;
  int skipfirst;
  int fliprg;
  int firstval;
  int port;
  int is_rgby;
  int oldport;
  int skittlequantity; // When LEDs are in a ring, backwards and forwards  This is the number of LEDs in the ring.
  char address[PARAM_BUFF];
  char oldaddress[PARAM_BUFF];
  struct sockaddr_in servaddr;
  int socket;
  int universe; // Universe to send packets to
  int8_t seq;
};


static void DPOUpdate(void * id, struct NoteFinder*nf)
{
  struct DPODriver * d = (struct DPODriver*)id;
  int i, j;
  e131_packet_t packet;
  e131_addr_t dest;
  uint8_t *buffer, *lbuff;

  if( strcmp( d->oldaddress, d->address ) != 0 || d->socket == -1 || d->oldport != d->port )
  {
    // create a socket for E1.31
    if ((d->socket = e131_socket()) < 0) {
      fprintf( stderr, "[E131] Failed to create socket\n" );
      return;
    }

    // ??? Required for colorchord ???
    d->oldport = d->port;
    memcpy(d->oldaddress, d->address, PARAM_BUFF);
  }

  if( d->socket > 0 ) {
    size_t buf_size = d->leds * (d->is_rgby ? 4 : 3);

    if ( (buffer = malloc(buf_size)) == NULL ) {
      fprintf( stderr, "Unable to allocate led buffer\n" );
      return;
    }
    if ( (lbuff = malloc(buf_size)) == NULL ) {
      fprintf( stderr, "Unable to allocate led buffer\n" );
      free(buffer);
      return;
    }

    d->firstval = 0;
    i = 0;
    while( i < d->skipfirst ) {
      lbuff[i] = d->firstval;
      buffer[i++] = d->firstval;
    }

    if( d->is_rgby ) {
      i = d->skipfirst;
      int k = 0;
      if( d->leds * 4 + i >= MAX_BUFFER ) {
        d->leds = (MAX_BUFFER-1)/4;
      }

      // Copy from OutLEDs[] into buffer, with size i.
      for( j = 0; j < d->leds; j++ ) {
        int r = OutLEDs[k++];
        int g = OutLEDs[k++];
        int b = OutLEDs[k++];
        int y = 0;
        int rg_common;
        if ( r/2 > g ) {
          rg_common = g;
        } else {
          rg_common = r/2;
        }

        if( rg_common > 255 ) {
          rg_common = 255;
        }

        y = rg_common;
        r -= rg_common;
        g -= rg_common;

        if( r < 0 ) {
          r = 0;
        }
        if( g < 0 ) {
          g = 0;
        }

        //Conversion from RGB to RAGB.  Consider: A is shifted toward RED.
        buffer[i++] = g; //Green
        buffer[i++] = r; //Red
        buffer[i++] = b; //Blue
        buffer[i++] = y; //Amber
      }
    } else {
      if( d->fliprg ) {
        for( j = 0; j < d->leds; j++ ) {
          lbuff[i++] = OutLEDs[j*3+1]; //GREEN??
          lbuff[i++] = OutLEDs[j*3+0]; //RED??
          lbuff[i++] = OutLEDs[j*3+2]; //BLUE
        }
      } else {
        for ( j = 0; j < d->leds; j++ ) {
          lbuff[i++] = OutLEDs[j*3+0];  //RED
          lbuff[i++] = OutLEDs[j*3+2];  //BLUE
          lbuff[i++] = OutLEDs[j*3+1];  //GREEN
        }
      }

      if( d->skittlequantity ) {
        i = d->skipfirst;
        for( j = 0; j < d->leds; j++ ) {
          int ledw = j;
          int ledpor = ledw % d->skittlequantity;
          int ol;

          if( ledw >= d->skittlequantity ) {
            ol = ledpor*2-1;
            ol = d->skittlequantity*2 - ol -2;
          } else {
            ol = ledpor*2;
          }

          buffer[i++] = lbuff[ol*3+0+d->skipfirst];
          buffer[i++] = lbuff[ol*3+1+d->skipfirst];
          buffer[i++] = lbuff[ol*3+2+d->skipfirst];
        }
      } else {
        memcpy( buffer, lbuff, i );
      }
    }

    // initialize the new E1.31 packet in universe 1 with 24 slots in preview mode
    e131_pkt_init(&packet, d->universe, MAX_BUFFER);
    memcpy(&packet.frame.source_name, "E1.31 ColorChord", 18);

    packet.frame.seq_number = d->seq;
    d->seq = (d->seq + 1) % 256;

    if (e131_set_option(&packet, E131_OPT_PREVIEW, true) < 0) {
      fprintf( stderr, "[E131] Failed to set option\n" );
      goto cleanup;
    }

    // set remote system destination as unicast address
    if (e131_unicast_dest(&dest, d->address, E131_DEFAULT_PORT) < 0) {
      fprintf( stderr, "[E131] Failed to set remote address\n" );
      goto cleanup;
    }

    // send buffer to universe
    for (size_t pos = 1; pos < MAX_BUFFER; pos++) {
      packet.dmp.prop_val[pos] = buffer[pos - 1];
    }

    if (e131_send(d->socket, &packet, &dest) < 0) {
      fprintf( stderr, "[E131] Failed to send packet\n" );
      close(d->socket);
      d->socket = -1;
      goto cleanup;
    }
    e131_pkt_dump(stderr, &packet);
  }

cleanup:
  free(buffer);
  free(lbuff);
}

static void DPOParams(void * id ) {
  struct DPODriver * d = (struct DPODriver*)id;
  strcpy( d->address, "localhost" );

  d->leds = 10;   RegisterValue(  "leds", PAINT, &d->leds, sizeof( d->leds ) );
  d->skipfirst = 1; RegisterValue(  "skipfirst", PAINT, &d->skipfirst, sizeof( d->skipfirst ) );
  d->port = 7777;   RegisterValue(  "port", PAINT, &d->port, sizeof( d->port ) );
  d->firstval = 0;  RegisterValue(  "firstval", PAINT, &d->firstval, sizeof( d->firstval ) );
            RegisterValue(  "address", PABUFFER, d->address, sizeof( d->address ) );
  d->fliprg = 0;    RegisterValue(  "fliprg", PAINT, &d->fliprg, sizeof( d->fliprg ) );
  d->is_rgby = 0;   RegisterValue(  "rgby", PAINT, &d->is_rgby, sizeof( d->is_rgby ) );
  d->skittlequantity=0;RegisterValue(  "skittlequantity", PAINT, &d->skittlequantity, sizeof( d->skittlequantity ) );
  d->universe=0;RegisterValue(  "universe", PAINT, &d->universe, sizeof( d->universe ) );
  d->socket = -1;
  d->oldaddress[0] = 0;
  d->seq = 1;
}

static struct DriverInstances * DisplayNetwork(const char * parameters) {
  struct DriverInstances * ret = malloc( sizeof( struct DriverInstances ) );
  struct DPODriver * d = ret->id = malloc( sizeof( struct DPODriver ) );
  memset( d, 0, sizeof( struct DPODriver ) );
  ret->Func = DPOUpdate;
  ret->Params = DPOParams;
  DPOParams( d );
  return ret;
}

REGISTER_OUT_DRIVER(DisplayNetwork);


