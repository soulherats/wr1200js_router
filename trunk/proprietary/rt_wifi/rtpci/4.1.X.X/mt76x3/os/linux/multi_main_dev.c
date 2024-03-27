/*
 ***************************************************************************
 * Ralink Tech Inc.
 * 4F, No. 2 Technology 5th Rd.
 * Science-based Industrial Park
 * Hsin-chu, Taiwan, R.O.C.
 *
 * (c) Copyright 2002-2004, Ralink Technology, Inc.
 *
 * All rights reserved. Ralink's source code is an unpublished work and the
 * use of a copyright notice does not imply otherwise. This source code
 * contains confidential trade secret material of Ralink Tech. Any attemp
 * or participation in deciphering, decoding, reverse engineering or in any
 * way altering the source code is stricitly prohibited, unless the prior
 * written consent of Ralink Technology, Inc. is obtained.
 ***************************************************************************

	Module Name:

	Abstract:

	Revision History:
	Who 		When			What
	--------	----------		----------------------------------------------
*/


#ifdef MULTI_INF_SUPPORT

#include "rtmp_comm.h"
#include "rt_os_util.h"
#include "rt_os_net.h"
#include <linux/pci.h>


/*
	Driver module load/unload function
*/
static int __init wifi_drv_init_module(void)
{
	int status = 0;

#ifdef RTMP_PCI_SUPPORT
	status = rt_pci_init_module();
	if (status)
		printk("Register PCI device driver failed(%d)!\n", status);
#endif /* RTMP_PCI_SUPPORT */



	return status;
}


static void __exit wifi_drv_cleanup_module(void)
{
//	int status = 0;

#ifdef RTMP_PCI_SUPPORT
	rt_pci_cleanup_module();
	printk("Unregister PCI device driver\n");
#endif /* RTMP_PCI_SUPPORT */


}


module_init(wifi_drv_init_module);
module_exit(wifi_drv_cleanup_module);

#endif /* MULTI_INF_SUPPORT */

#ifdef APCLI_SUPPORT
/*
    ========================================================================

    Routine Description:
        return ethernet statistics counter

    Arguments:
        net_dev                     Pointer to net_device

    Return Value:
        net_device_stats*

    Note:

    ========================================================================
*/
struct net_device_stats *RT28xx_get_apcli_ether_stats(
    IN PNET_DEV net_dev)
{
    VOID *pAd = NULL;
    struct net_device_stats *pStats;

    if (net_dev)
        GET_PAD_FROM_NET_DEV(pAd, net_dev);

    if (pAd)
    {
        RT_CMD_STATS ApCliStats, *pApCliStats = &ApCliStats;

        pApCliStats->pNetDev = net_dev;

        if (RTMP_COM_IoctlHandle(pAd, NULL, CMD_RTPRIV_IOCTL_APCLI_STATS_GET,
                0, pApCliStats, RT_DEV_PRIV_FLAGS_GET(net_dev)) != NDIS_STATUS_SUCCESS)
        {
            return NULL;
        }

        pStats = (struct net_device_stats *)pApCliStats->pStats; /*pAd->stats; */

        pStats->rx_packets = pApCliStats->rx_packets;
        pStats->tx_packets = pApCliStats->tx_packets;

        pStats->rx_bytes = pApCliStats->rx_bytes;
        pStats->tx_bytes = pApCliStats->tx_bytes;

        pStats->rx_errors = pApCliStats->rx_errors;
        pStats->tx_errors = pApCliStats->tx_errors;

        pStats->rx_dropped = 0;
        pStats->tx_dropped = 0;

          pStats->multicast = pApCliStats->multicast;
          pStats->collisions = pApCliStats->collisions;

          pStats->rx_length_errors = 0;
          pStats->rx_over_errors = pApCliStats->rx_over_errors;
          pStats->rx_crc_errors = 0;
          pStats->rx_frame_errors = pApCliStats->rx_frame_errors;
          pStats->rx_fifo_errors = pApCliStats->rx_fifo_errors;
          pStats->rx_missed_errors = 0; /* receiver missed packet */

        /* detailed tx_errors */
          pStats->tx_aborted_errors = 0;
          pStats->tx_carrier_errors = 0;
          pStats->tx_fifo_errors = 0;
          pStats->tx_heartbeat_errors = 0;
          pStats->tx_window_errors = 0;

        /* for cslip etc */
          pStats->rx_compressed = 0;
          pStats->tx_compressed = 0;

        return pStats;
    }
    else
        return NULL;
}
#endif /* APCLI_SUPPORT */

