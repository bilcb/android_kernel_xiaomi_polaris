/*
 * Author: andip71, 01.09.2017
 *
 * Version 1.1.0
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef _BOEFFLA_WL_BLOCKER_H
#define _BOEFFLA_WL_BLOCKER_H

#define BOEFFLA_WL_BLOCKER_VERSION	"1.1.0"

#define LIST_WL_DEFAULT				"qcom_rx_wakelock;wlan;wlan_wow_wl;wlan_extscan_wl;netmgr_wl;NETLINK"

#define LENGTH_LIST_WL		255
#define LENGTH_LIST_WL_DEFAULT	100
#define LENGTH_LIST_WL_SEARCH	(LENGTH_LIST_WL + \
				 LENGTH_LIST_WL_DEFAULT + 5)

extern char list_wl[LENGTH_LIST_WL];
extern char list_wl_default[LENGTH_LIST_WL_DEFAULT];
extern char list_wl_search[LENGTH_LIST_WL_SEARCH];
extern bool wl_blocker_active;
extern bool wl_blocker_debug;

/*
 * sdm845-tuned extended default list (NOT compiled in; apply at runtime
 * instead if wanted). Write it as a single line to the USER list file:
 *   echo "<list>" > .../boeffla_wakelock_blocker/wakelock_blocker
 * (243 chars, fits LENGTH_LIST_WL=255; entries duplicated with the
 * compiled-in default list are harmless. Writing it to
 * wakelock_blocker_default fails with -EINVAL: that file is limited to
 * LENGTH_LIST_WL_DEFAULT=100):
 *
 * qcom_rx_wakelock;wlan;wlan_wow_wl;wlan_extscan_wl;netmgr_wl;NETLINK;IPA_WS;
 * [timerfd];wlan_ipa;wlan_pno_wl;wcnss_filter_lock;IPCRTR_lpass_rx;
 * hal_bluetooth_lock;898000.qcom,qup_uart;
 * c440000.qcom,spmi:qcom,pmi8998@2:qcom,qpnp-smb2;bluetooth_timer
 */

#endif /* _BOEFFLA_WL_BLOCKER_H */
