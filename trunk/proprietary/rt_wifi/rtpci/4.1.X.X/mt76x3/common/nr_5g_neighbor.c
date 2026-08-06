/*
 * 5G cross-band neighbor table for 802.11k Neighbor Report.
 */
#ifdef DOT11K_RRM_SUPPORT

#include "rt_config.h"
#include <linux/proc_fs.h>
#include <asm/uaccess.h>

NR_5G_NEIGHBOR_ENTRY g_5g_neighbor_tab[MAX_5G_NEIGHBOR_NUM];
UINT8 g_5g_neighbor_cnt = 0;
DEFINE_SPINLOCK(g_5g_neighbor_lock);

#ifdef CONFIG_PROC_FS

extern struct proc_dir_entry *proc_ralink_wl;
extern struct proc_dir_entry *procRegDir;

static struct proc_dir_entry *entry_5g_neighbor;

static ssize_t nr_5g_neighbor_write(
	struct file *file,
	const char __user *buffer,
	size_t count,
	loff_t *ppos)
{
	char *buf, *p;
	int i, k, n;
	unsigned int b[6];
	unsigned int ch, rc, pt, reach, sec, qos, ht, vht, rrm, apsd;
	unsigned long flags;

	if (!count || count > 512)
		return -EINVAL;
	buf = kmalloc(count + 1, GFP_KERNEL);
	if (!buf) return -ENOMEM;
	if (copy_from_user(buf, buffer, count)) { kfree(buf); return -EFAULT; }
	buf[count] = '\0';
	if (count > 0 && buf[count - 1] == '\n')
		buf[count - 1] = '\0';

	if (strcmp(buf, "clear") == 0 || strcmp(buf, "delete") == 0) {
		spin_lock_irqsave(&g_5g_neighbor_lock, flags);
		for (i = 0; i < MAX_5G_NEIGHBOR_NUM; i++)
			g_5g_neighbor_tab[i].bValid = FALSE;
		g_5g_neighbor_cnt = 0;
		spin_unlock_irqrestore(&g_5g_neighbor_lock, flags);
		kfree(buf); return count;
	}

	p = buf;
	while (*p == ' ' || *p == '\t') p++;

	if (sscanf(p, "%x:%x:%x:%x:%x:%x %u %u %u %u %u %u %u %u %u %u%n",
		&b[0], &b[1], &b[2], &b[3], &b[4], &b[5],
		&ch, &rc, &pt, &reach, &sec, &qos, &ht, &vht, &rrm, &apsd, &n) < 16)
	{
		DBGPRINT(RT_DEBUG_ERROR, ("%s: parse failed\n", __func__));
		kfree(buf); return -EINVAL;
	}

	for (k = 0; k < 6; k++) {
		if (b[k] > 0xff) {
			kfree(buf);
			return -EINVAL;
		}
	}
	if (!ch || ch > 255 || rc > 255 || pt > 255 || reach > 3 ||
	    sec > 1 || qos > 1 || ht > 1 || vht > 1 || rrm > 1 || apsd > 1) {
		kfree(buf);
		return -EINVAL;
	}

	spin_lock_irqsave(&g_5g_neighbor_lock, flags);
	for (i = 0; i < MAX_5G_NEIGHBOR_NUM; i++)
		if (!g_5g_neighbor_tab[i].bValid)
			break;
	if (i >= MAX_5G_NEIGHBOR_NUM) {
		spin_unlock_irqrestore(&g_5g_neighbor_lock, flags);
		kfree(buf);
		return -ENOSPC;
	}

	g_5g_neighbor_tab[i].Bssid[0] = (UCHAR)b[0];
	g_5g_neighbor_tab[i].Bssid[1] = (UCHAR)b[1];
	g_5g_neighbor_tab[i].Bssid[2] = (UCHAR)b[2];
	g_5g_neighbor_tab[i].Bssid[3] = (UCHAR)b[3];
	g_5g_neighbor_tab[i].Bssid[4] = (UCHAR)b[4];
	g_5g_neighbor_tab[i].Bssid[5] = (UCHAR)b[5];
	g_5g_neighbor_tab[i].Channel = (UINT8)ch;
	g_5g_neighbor_tab[i].RegulatoryClass = (UINT8)rc;
	g_5g_neighbor_tab[i].PhyType = (UINT8)pt;
	g_5g_neighbor_tab[i].APReachable = (UINT8)reach;
	g_5g_neighbor_tab[i].bSecurity = (UINT8)sec;
	g_5g_neighbor_tab[i].bQos = (UINT8)qos;
	g_5g_neighbor_tab[i].bHT = (UINT8)ht;
	g_5g_neighbor_tab[i].bVHT = (UINT8)vht;
	g_5g_neighbor_tab[i].bRRM = (UINT8)rrm;
	g_5g_neighbor_tab[i].bAPSD = (UINT8)apsd;

	/* Extract SSID - everything after the 16 parsed fields */
	g_5g_neighbor_tab[i].SsidLen = 0;
	memset(g_5g_neighbor_tab[i].Ssid, 0, sizeof(g_5g_neighbor_tab[i].Ssid));
	{
		size_t slen;
		p += n;
		while (*p == ' ' || *p == '\t') p++;
		slen = strlen(p);
		if (slen > MAX_LEN_OF_SSID) slen = MAX_LEN_OF_SSID;
		if (slen) {
			memcpy(g_5g_neighbor_tab[i].Ssid, p, slen);
			g_5g_neighbor_tab[i].SsidLen = (UINT8)slen;
		}
	}

	g_5g_neighbor_tab[i].bValid = TRUE;
	g_5g_neighbor_cnt++;
	spin_unlock_irqrestore(&g_5g_neighbor_lock, flags);

	DBGPRINT(RT_DEBUG_TRACE, ("%s: added %02x:%02x:%02x:%02x:%02x:%02x ch=%d\n",
		__func__, b[0], b[1], b[2], b[3], b[4], b[5], ch));

	kfree(buf);
	return count;
}

static ssize_t nr_5g_neighbor_read(
	char *page, char **start, off_t off,
	int count, int *eof, void *data_unused)
{
	int len = 0, i, written;
	unsigned long flags;

	if (count <= 0)
		return 0;

	spin_lock_irqsave(&g_5g_neighbor_lock, flags);
	written = snprintf(page, count,
		"5G Neighbor Table (%d entries, max %d)\n"
		"BSSID            CH  RC  PT  Rch Sec Qos HT VHT RRM APSD SSID\n",
		g_5g_neighbor_cnt, MAX_5G_NEIGHBOR_NUM);
	if (written >= count) {
		len = count;
		goto out;
	}
	len = written;

	for (i = 0; i < MAX_5G_NEIGHBOR_NUM; i++) {
		if (!g_5g_neighbor_tab[i].bValid)
			continue;
		written = snprintf(page + len, count - len,
			"%02x:%02x:%02x:%02x:%02x:%02x %3u %3u %3u  %3u  %3u %3u %3u %3u %3u %3u  %.*s\n",
			g_5g_neighbor_tab[i].Bssid[0], g_5g_neighbor_tab[i].Bssid[1],
			g_5g_neighbor_tab[i].Bssid[2], g_5g_neighbor_tab[i].Bssid[3],
			g_5g_neighbor_tab[i].Bssid[4], g_5g_neighbor_tab[i].Bssid[5],
			g_5g_neighbor_tab[i].Channel, g_5g_neighbor_tab[i].RegulatoryClass,
			g_5g_neighbor_tab[i].PhyType, g_5g_neighbor_tab[i].APReachable,
			g_5g_neighbor_tab[i].bSecurity, g_5g_neighbor_tab[i].bQos,
			g_5g_neighbor_tab[i].bHT, g_5g_neighbor_tab[i].bVHT,
			g_5g_neighbor_tab[i].bRRM, g_5g_neighbor_tab[i].bAPSD,
			g_5g_neighbor_tab[i].SsidLen, g_5g_neighbor_tab[i].Ssid);
		if (written >= count - len) {
			len = count;
			break;
		}
		len += written;
	}
out:
	spin_unlock_irqrestore(&g_5g_neighbor_lock, flags);
	*eof = 1;
	return len;
}

int nr_5g_neighbor_proc_init(void)
{
	if (!proc_ralink_wl) return -1;
	entry_5g_neighbor = create_proc_entry("5g_neighbor", 0644, proc_ralink_wl);
	if (entry_5g_neighbor) {
		entry_5g_neighbor->read_proc = (read_proc_t *)&nr_5g_neighbor_read;
		entry_5g_neighbor->write_proc = (write_proc_t *)&nr_5g_neighbor_write;
	}
	return 0;
}

void nr_5g_neighbor_proc_exit(void)
{
	if (entry_5g_neighbor) {
		remove_proc_entry("5g_neighbor", proc_ralink_wl);
		entry_5g_neighbor = NULL;
	}
}
#else
int nr_5g_neighbor_proc_init(void) { return 0; }
void nr_5g_neighbor_proc_exit(void) {}
#endif /* CONFIG_PROC_FS */
#endif /* DOT11K_RRM_SUPPORT */
