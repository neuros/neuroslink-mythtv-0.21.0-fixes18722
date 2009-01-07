/*
 * 6200ch - an external channel changer for Motorola DCT-6200 Tuner 
 * 
 * Copyright 2004,2005 by Stacey D. Son <mythdev@son.org> 
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <libavc1394/rom1394.h>
#include <libavc1394/avc1394.h>
#include <libraw1394/raw1394.h>
#include <sys/types.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <limits.h> //for LLONG_MAX
#include <getopt.h>
#include <unistd.h> // for usleep

// Vendor and Model IDs.
// NOTE: Some Models have more than one possible Vendor ID
// WARNING: Please update firewiredevice.cpp when adding to this list.

#define DCH3200_VENDOR_ID1 0x00001c11
#define DCH3200_MODEL_ID1  0x0000d330

#define DCT3412_VENDOR_ID1 0x0000159a
#define DCT3412_MODEL_ID1  0x000034cb

#define DCT3416_VENDOR_ID1 0x000016b5
#define DCT3416_VENDOR_ID2 0x00001bdd
#define DCT3416_MODEL_ID1  0x0000346b
#define DCT3416_MODEL_ID2  0x0000b630

#define DCT5100_VENDOR_ID1 0x000017ee
#define DCT5100_MODEL_ID1  0x0000620a

#define DCT6200_VENDOR_ID1 0x00000ce5
#define DCT6200_VENDOR_ID2 0x00000e5c
#define DCT6200_VENDOR_ID3 0x00001225
#define DCT6200_VENDOR_ID4 0x00000f9f
#define DCT6200_VENDOR_ID5 0x00001180
#define DCT6200_VENDOR_ID6 0x000012c9
#define DCT6200_VENDOR_ID7 0x000011ae
#define DCT6200_VENDOR_ID8 0x0000152f
#define DCT6200_VENDOR_ID9 0x000014e8
#define DCT6200_VENDOR_ID10 0x000016b5
#define DCT6200_VENDOR_ID11 0x00001371
#define DCT6200_VENDOR_ID12 0x000019a6
#define DCT6200_VENDOR_ID13 0x00001aad
#define DCT6200_VENDOR_ID14 0x00000b06
#define DCT6200_VENDOR_ID15 0x0000195e
#define DCT6200_SPEC_ID    0x00005068
#define DCT6200_SW_VERSION 0x00010101
#define DCT6200_MODEL_ID1  0x0000620a
#define DCT6200_MODEL_ID2  0x00006200

#define DCT6412_VENDOR_ID1 0x00000f9f
#define DCT6412_VENDOR_ID2 0x0000152f
#define DCT6412_MODEL_ID1  0x000064ca
#define DCT6412_MODEL_ID2  0x000064cb

#define DCT6416_VENDOR_ID1 0x000017ee
#define DCT6416_VENDOR_ID2 0x00001a66 
#define DCT6416_MODEL_ID1  0x0000646b

#define PACE_VENDOR_ID1    0x00005094 /* 550 & 779 */
#define PACE550_MODEL_ID1  0x00010551
#define PACE779_MODEL_ID1  0x00010755

#define AVC1394_6200_OPERAND_SET 0x20      /* 6200 subunit command operand */

#define CTL_CMD0 AVC1394_CTYPE_CONTROL | AVC1394_SUBUNIT_TYPE_PANEL | \
        AVC1394_SUBUNIT_ID_0 | AVC1394_PANEL_COMMAND_PASS_THROUGH | \
        AVC1394_6200_OPERAND_SET

#define STARTING_NODE 1  /* skip 1394 nodes to avoid error msgs */
#define STARTING_PORT 0
#define RETRY_COUNT_SLOW 1
#define RETRY_COUNT_FAST 0

void set_chan_slow(raw1394handle_t handle, int device, int verbose, int chn);
void set_chan_fast(raw1394handle_t handle, int device, int verbose, int chn);

void usage()
{
   fprintf(stderr, "Usage: 6200ch [-v] [-s] [-n NODE] [-g GUID] [-p PORT] "
           "<channel_num>\n");
   fprintf(stderr, "-v        print additional verbose output\n");
   fprintf(stderr, "-s        use single packet method\n");
   fprintf(stderr, "-n NODE   node to start device scanning on (default:%i)\n",
           STARTING_NODE);
   fprintf(stderr, "-p PORT   port/adapter to use              (default:%i)\n",
           STARTING_PORT);
   fprintf(stderr, "-g GUID   GUID to use, -n switch, if present, will be ignored.\n");
   exit(1);
}

int main (int argc, char *argv[])
{
   rom1394_directory dir;
   int device = -1;
   int i;
   int verbose = 0;
   int single_packet = 0;
   int bGUID=0;
   octlet_t cli_GUID=0LL;
   octlet_t node_GUID=0LL;
   quadlet_t cmd[2];
   int chn = 550;

   /* some people experience crashes when starting on node 1 */
   int starting_node = STARTING_NODE;
   int starting_port = STARTING_PORT;
   int c;
   int index;

   if (argc < 2) 
      usage();

   opterr = 0;
   while ((c = getopt(argc, argv, "vsg:n:p:")) != -1)
   {
       switch (c) {
       case 'v':
           verbose = 1;
           break;
       case 's':
           single_packet = 1;
           break;
       case 'n':
           starting_node = atoi(optarg);
           break;
       case 'g':
           bGUID=1;
	   starting_node=0;
           cli_GUID = (octlet_t)strtoll(optarg, (char **)NULL, 16);
           break;
       case 'p':
           starting_port = atoi(optarg);
           break;
       default:
           fprintf(stderr, "incorrect command line arguments\n");
           usage();
       }
   }

   /* print out usage message if not enough arguments */
   if (optind != argc-1) {
       usage();
   }
   /* the last argument is the channel number */
   chn = atoi(argv[optind]);

#ifdef RAW1394_V_0_8
   raw1394handle_t handle = raw1394_get_handle();
#else
   raw1394handle_t handle = raw1394_new_handle();
#endif

   if (!handle) {
      if (!errno) {
         fprintf(stderr, "Not Compatable!\n");
      } else {
         perror("Couldn't get 1394 handle");
         fprintf(stderr, "Is ieee1394, driver, and raw1394 loaded?\n");
      }
      exit(1);
   } 

   if (raw1394_set_port(handle, starting_port) < 0) {
      perror("couldn't set port");
      raw1394_destroy_handle(handle);
      exit(1);
   }

   if (verbose)
       printf("starting with node: %d\n", starting_node);

   int nc = raw1394_get_nodecount(handle);
   if (bGUID!=0) {
      if (cli_GUID==0LL || cli_GUID==LLONG_MAX || cli_GUID==LLONG_MIN) {
	  fprintf(stderr, "error parsing GUID command line parameter\n");
          exit(1);
      }	  
   }	  
   for (i=starting_node; i < nc; ++i) {
      if (bGUID!=0) {
         node_GUID=rom1394_get_guid(handle, i);
#ifdef DEBUG
	 printf("node=%d, node_GUID=%LX, cli_GUID=%LX\n", i, node_GUID, cli_GUID);
#endif	 
         if (cli_GUID!=node_GUID) {
             continue;
         }
      } 

      if (rom1394_get_directory(handle, i, &dir) < 0) {
         fprintf(stderr,"error reading config rom directory for node %d\n", i);
         raw1394_destroy_handle(handle);
         exit(1);
      }

      if (verbose) 
         printf("node %d: vendor_id = 0x%08x model_id = 0x%08x\n", 
                 i, dir.vendor_id, dir.model_id); 

      // WARNING: Please update firewiredevice.cpp when adding to this list.
      if ( ((dir.vendor_id == DCH3200_VENDOR_ID1) ||
            (dir.vendor_id == DCT3412_VENDOR_ID1) || 
            (dir.vendor_id == DCT3416_VENDOR_ID1) || 
            (dir.vendor_id == DCT3416_VENDOR_ID2) || 
            (dir.vendor_id == DCT5100_VENDOR_ID1) ||
            (dir.vendor_id == DCT6200_VENDOR_ID1) ||
            (dir.vendor_id == DCT6200_VENDOR_ID2) ||
            (dir.vendor_id == DCT6200_VENDOR_ID3) ||
            (dir.vendor_id == DCT6200_VENDOR_ID4) ||
            (dir.vendor_id == DCT6200_VENDOR_ID5) ||
            (dir.vendor_id == DCT6200_VENDOR_ID6) ||
            (dir.vendor_id == DCT6200_VENDOR_ID7) ||
            (dir.vendor_id == DCT6200_VENDOR_ID8) ||
            (dir.vendor_id == DCT6200_VENDOR_ID9) ||
            (dir.vendor_id == DCT6200_VENDOR_ID10) ||
            (dir.vendor_id == DCT6200_VENDOR_ID11) ||
            (dir.vendor_id == DCT6200_VENDOR_ID12) ||
            (dir.vendor_id == DCT6200_VENDOR_ID13) ||
            (dir.vendor_id == DCT6200_VENDOR_ID14) ||
            (dir.vendor_id == DCT6200_VENDOR_ID15) ||
            (dir.vendor_id == DCT6412_VENDOR_ID1) ||
            (dir.vendor_id == DCT6412_VENDOR_ID2) ||
            (dir.vendor_id == DCT6416_VENDOR_ID1) || 
            (dir.vendor_id == DCT6416_VENDOR_ID2) ||
            (dir.vendor_id == PACE_VENDOR_ID1)) &&
           ((dir.model_id == DCH3200_MODEL_ID1) ||
            (dir.model_id == DCT3412_MODEL_ID1) ||
            (dir.model_id == DCT3416_MODEL_ID1) ||
            (dir.model_id == DCT3416_MODEL_ID2) ||
            (dir.model_id == DCT5100_MODEL_ID1) ||
            (dir.model_id == DCT6200_MODEL_ID1) ||
            (dir.model_id == DCT6200_MODEL_ID2) ||
            (dir.model_id == DCT6412_MODEL_ID1) ||
            (dir.model_id == DCT6412_MODEL_ID2) ||
            (dir.model_id == DCT6416_MODEL_ID1) ||
            (dir.model_id == PACE550_MODEL_ID1) ||
            (dir.model_id == PACE779_MODEL_ID1)) )
      {
            if (dir.unit_spec_id != DCT6200_SPEC_ID)
               fprintf(stderr, "Warning: Unit Spec ID different.\n");
            if (dir.unit_sw_version != DCT6200_SW_VERSION)
               fprintf(stderr, "Warning: Unit Software Version different.\n");
            device = i;
            break;
      }
   }
    
   if (device == -1) {
        fprintf(stderr, "Could not find Motorola DCT-6200 on the 1394 bus.\n");
        raw1394_destroy_handle(handle);
        exit(1);
   }

   if (single_packet)
       set_chan_fast(handle, device, verbose, chn);
   else
       set_chan_slow(handle, device, verbose, chn);

   raw1394_destroy_handle(handle);
   exit(0);
}

void set_chan_slow(raw1394handle_t handle, int device, int verbose, int chn)
{
   int i;
   int dig[3];
   quadlet_t cmd[2];

   dig[2] = (chn % 10);
   dig[1] = (chn % 100)  / 10;
   dig[0] = (chn % 1000) / 100;

   if (verbose)
      printf("AV/C Command: %d%d%d = Op1=0x%08X Op2=0x%08X Op3=0x%08X\n", 
            dig[0], dig[1], dig[2], 
            CTL_CMD0 | dig[0], CTL_CMD0 | dig[1], CTL_CMD0 | dig[2]);

   for (i=0; i<3; i++) {
      cmd[0] = CTL_CMD0 | dig[i];
      cmd[1] = 0x0;
    
      avc1394_transaction_block(handle, device, cmd, 2, RETRY_COUNT_SLOW);
      usleep(500000);  // small delay for button to register
   }
}

void set_chan_fast(raw1394handle_t handle, int device, int verbose, int chn)
{
    quadlet_t cmd[3];

    cmd[0] = CTL_CMD0 | 0x67;
    cmd[1] = (0x04 << 24) | (chn << 8) | 0x000000FF;
    cmd[2] = 0xFF << 24;

    if (verbose)
        printf("AV/C command for channel %d = 0x%08X %08X %08X\n", 
               chn, cmd[0], cmd[1], cmd[2]);
 
    avc1394_transaction_block(handle, device, cmd, 3, RETRY_COUNT_FAST);
}

/* vim: set expandtab tabstop=4 shiftwidth=4: */
