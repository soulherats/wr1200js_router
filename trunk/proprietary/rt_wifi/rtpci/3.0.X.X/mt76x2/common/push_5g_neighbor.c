#include "rt_config.h"

#if defined(DOT11K_RRM_SUPPORT)

#define NEIGHBOR_PROC_PATH	"/proc/mt7621/wl/5g_neighbor"

static UINT8 neighbor_regulatory_class(UINT8 channel)
{
	if (channel <= 48)
		return 115;
	if (channel <= 64)
		return 118;
	if (channel <= 144)
		return 121;
	if (channel <= 161)
		return 124;
	return 125;
}

int push_5g_ap_info(RTMP_ADAPTER *pAd)
{
	RTMP_OS_FD fd;
	RTMP_OS_FS_INFO osFSInfo;
	CHAR buf[256];
	int len;
	UINT8 phy_type;
	UINT8 ht, vht;
	MULTISSID_STRUCT *pMbss = &pAd->ApCfg.MBSSID[0];

	if (pAd->CommonCfg.Channel <= 14)
		return 0;

	/* PHY_11VHT_* modes start at 12 */
	if (pAd->CommonCfg.PhyMode >= 12) {
		phy_type = 9;	/* RRM PHY type: VHT */
		ht = 1;
		vht = 1;
	} else if (pAd->CommonCfg.PhyMode == PHY_11A) {
		phy_type = 4;	/* RRM PHY type: OFDM */
		ht = 0;
		vht = 0;
	} else {
		/* AN_MIXED / N_5G / AGN_MIXED etc. */
		phy_type = 7;	/* RRM PHY type: HT */
		ht = 1;
		vht = 0;
	}

	/* field order must match nr_5g_neighbor_write sscanf:
	 * BSSID ch rc pt reach sec qos ht vht rrm apsd SSID
	 * QoS bit in CapabilityInfo is intentionally unset per WMM spec v1.1;
	 * read wdev.bWmmCapable and UapsdInfo.bAPSDCapable directly. */
	len = snprintf(buf, sizeof(buf),
		"%02x:%02x:%02x:%02x:%02x:%02x "
		"%d %d %d 3 %d %d %d %d 1 %d %.*s\n",
		pMbss->wdev.bssid[0], pMbss->wdev.bssid[1],
		pMbss->wdev.bssid[2], pMbss->wdev.bssid[3],
		pMbss->wdev.bssid[4], pMbss->wdev.bssid[5],
		pAd->CommonCfg.Channel,
		neighbor_regulatory_class(pAd->CommonCfg.Channel), phy_type,
		(pMbss->wdev.WepStatus != 0) ? 1 : 0,
		pMbss->wdev.bWmmCapable ? 1 : 0,
		ht, vht,
		pMbss->UapsdInfo.bAPSDCapable ? 1 : 0,
		pMbss->SsidLen, pMbss->Ssid);

	if (len >= sizeof(buf))
		return -ENOSPC;

	RtmpOSFSInfoChange(&osFSInfo, TRUE);

	fd = RtmpOSFileOpen(NEIGHBOR_PROC_PATH, RTMP_FILE_WRONLY, 0);
	if (IS_ERR((void *)fd)) {
		RtmpOSFSInfoChange(&osFSInfo, FALSE);
		return -ENOENT;
	}
	RtmpOSFileWrite(fd, "clear\n", 6);
	RtmpOSFileClose(fd);

	fd = RtmpOSFileOpen(NEIGHBOR_PROC_PATH, RTMP_FILE_WRONLY, 0);
	if (IS_ERR((void *)fd)) {
		RtmpOSFSInfoChange(&osFSInfo, FALSE);
		return -ENOENT;
	}
	RtmpOSFileSeek(fd, 0);
	RtmpOSFileWrite(fd, buf, len);
	RtmpOSFileClose(fd);

	RtmpOSFSInfoChange(&osFSInfo, FALSE);
	return 0;
}

#endif /* DOT11K_RRM_SUPPORT */
