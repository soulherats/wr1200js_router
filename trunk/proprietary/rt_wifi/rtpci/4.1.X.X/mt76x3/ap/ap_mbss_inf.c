/****************************************************************************
 * Ralink Tech Inc.
 * Taiwan, R.O.C.
 *
 * (c) Copyright 2002, Ralink Technology, Inc.
 *
 * All rights reserved. Ralink's source code is an unpublished work and the
 * use of a copyright notice does not imply otherwise. This source code
 * contains confidential trade secret material of Ralink Tech. Any attemp
 * or participation in deciphering, decoding, reverse engineering or in any
 * way altering the source code is stricitly prohibited, unless the prior
 * written consent of Ralink Technology, Inc. is obtained.
 ***************************************************************************/

/****************************************************************************

    Abstract:

    Support multi-BSS function.

    Note:
    1. Call RT28xx_MBSS_Init() in init function and
       call RT28xx_MBSS_Remove() in close function

    2. MAC of different BSS is initialized in APStartUp()

    3. BSS Index (0 ~ 15) of different rx packet is got in

    4. BSS Index (0 ~ 15) of different tx packet is assigned in

    5. BSS Index (0 ~ 15) of different BSS is got in APHardTransmit() by using

    6. BSS Index (0 ~ 15) of IOCTL command is put in pAd->OS_Cookie->ioctl_if

    7. Beacon of different BSS is enabled in APMakeAllBssBeacon() by writing 1
       to the register MAC_BSSID_DW1

    8. The number of MBSS can be 1, 2, 4, or 8

***************************************************************************/
#define RTMP_MODULE_OS

#ifdef MBSS_SUPPORT

/*#include "rt_config.h" */
#include "rtmp_comm.h"
#include "rt_os_util.h"
#include "rt_os_net.h"


/* --------------------------------- Public -------------------------------- */
NET_DEV_STATS *RT28xx_get_ether_stats(PNET_DEV net_dev);
struct rtnl_link_stats64 *RT28xx_get_ether_stats64(struct net_device *net_dev, struct rtnl_link_stats64 *stats);

/*
========================================================================
Routine Description:
    Initialize Multi-BSS function.

Arguments:
    pAd				points to our adapter
    pDevMain		points to the main BSS network interface

Return Value:
    None

Note:
	1. Only create and initialize virtual network interfaces.
	2. No main network interface here.
	3. If you down ra0 and modify the BssNum of RT2860AP.dat/RT2870AP.dat,
		it will not work! You must rmmod rt2860ap.ko and lsmod rt2860ap.ko again.
========================================================================
*/
VOID RT28xx_MBSS_Init(VOID *pAd, PNET_DEV pDevMain)
{
	RTMP_OS_NETDEV_OP_HOOK netDevHook;

	NdisZeroMemory(&netDevHook, sizeof(netDevHook));
	netDevHook.open = MBSS_VirtualIF_Open;	/* device opem hook point */
	netDevHook.stop = MBSS_VirtualIF_Close;	/* device close hook point */
	netDevHook.xmit = rt28xx_send_packets;	/* hard transmit hook point */
	netDevHook.ioctl = rt28xx_ioctl;	/* ioctl hook point */
	netDevHook.get_stats64 = RT28xx_get_ether_stats64;

	RTMP_AP_IoctlHandle(pAd, NULL, CMD_RTPRIV_IOCTL_MBSS_INIT,
						0, &netDevHook, 0);

}


/*
========================================================================
Routine Description:
    Remove Multi-BSS network interface.

Arguments:
	pAd			points to our adapter

Return Value:
    None

Note:
    FIRST_MBSSID = 1
    Main BSS is not removed here.
========================================================================
*/
VOID RT28xx_MBSS_Remove(VOID *pAd)
{

	RTMP_AP_IoctlHandle(pAd, NULL, CMD_RTPRIV_IOCTL_MBSS_REMOVE, 0, NULL, 0);

}



/* --------------------------------- Private -------------------------------- */
/*
========================================================================
Routine Description:
    Open a virtual network interface.

Arguments:
	pDev			which WLAN network interface

Return Value:
    0: open successfully
    otherwise: open fail

Note:
========================================================================
*/
INT MBSS_VirtualIF_Open(PNET_DEV pDev)
{
	VOID *pAd;

	/* increase MODULE use count */
	RT_MOD_INC_USE_COUNT();

	DBGPRINT(RT_DEBUG_TRACE, ("%s: ===> MBSSVirtualIF_open\n", RTMP_OS_NETDEV_GET_DEVNAME(pDev)));

	pAd = RTMP_OS_NETDEV_GET_PRIV(pDev);

	RTMP_AP_IoctlHandle(pAd, NULL, CMD_RTPRIV_IOCTL_MBSS_OPEN, 0, pDev, 0);

	if (VIRTUAL_IF_UP(pAd) != 0)
		return -1;

#ifdef MT_MAC
    RTMP_AP_IoctlHandle(pAd, NULL, CMD_RTPRIV_IOCTL_MBSS_CR_ENABLE, 0, pDev, 0);
#ifdef CONFIG_RA_HW_NAT_WIFI_NEW_ARCH
	RT_MOD_HNAT_REG(pDev);
#endif
#endif

	RTMP_OS_NETDEV_START_QUEUE(pDev);

	return 0;
}


/*
========================================================================
Routine Description:
    Close a virtual network interface.

Arguments:
    pDev           which WLAN network interface

Return Value:
    0: close successfully
    otherwise: close fail

Note:
========================================================================
*/
INT MBSS_VirtualIF_Close(PNET_DEV pDev)
{
	VOID *pAd;


	DBGPRINT(RT_DEBUG_TRACE, ("%s: ===> MBSSVirtualIF_close\n", RTMP_OS_NETDEV_GET_DEVNAME(pDev)));

	pAd = RTMP_OS_NETDEV_GET_PRIV(pDev);

#ifdef MT_MAC
    RTMP_AP_IoctlHandle(pAd, NULL, CMD_RTPRIV_IOCTL_MBSS_CR_DISABLE, 0, pDev, 0);
#endif

	RTMP_OS_NETDEV_STOP_QUEUE(pDev);

	RTMP_AP_IoctlHandle(pAd, NULL, CMD_RTPRIV_IOCTL_MBSS_CLOSE, 0, pDev, 0);

#ifdef CONFIG_RA_HW_NAT_WIFI_NEW_ARCH
	RT_MOD_HNAT_DEREG(pDev);
#endif
	VIRTUAL_IF_DOWN(pAd);

	RT_MOD_DEC_USE_COUNT();
	return 0;
}

#endif /* MBSS_SUPPORT */

