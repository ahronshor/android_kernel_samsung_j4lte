/*****************************************************************************
 *
 * Copyright (c) 2017 Samsung Electronics Co., Ltd. All rights reserved
 *
 *****************************************************************************/
#include <net/cfg80211.h>
#include <linux/etherdevice.h>
#include "dev.h"
#include "fapi.h"
#include "fw_test.h"
#include "debug.h"
#include "mgt.h"
#include "mlme.h"
#include "netif.h"
#include "ba.h"

static void slsi_fw_test_save_frame(struct slsi_dev *sdev, struct slsi_fw_test *fwtest, struct sk_buff *saved_skbs[CONFIG_SCSC_WLAN_MAX_INTERFACES + 1], struct sk_buff *skb, bool udi_header)
{
	u16 vif;

	skb = slsi_skb_copy(skb, GFP_KERNEL);

	if (udi_header)
		skb_pull(skb, sizeof(struct udi_msg_t));

	vif = fapi_get_vif(skb);

	SLSI_DBG3(sdev, SLSI_FW_TEST, "sig:0x%.4X, vif:%d", fapi_get_sigid(skb), vif);
	slsi_debug_frame(sdev, NULL, skb, "SAVE");

	slsi_spinlock_lock(&fwtest->fw_test_lock);
	slsi_kfree_skb(saved_skbs[vif]);
	saved_skbs[vif] = skb;
	slsi_spinlock_unlock(&fwtest->fw_test_lock);
}

static void slsi_fw_test_process_frame(struct slsi_dev *sdev, struct slsi_fw_test *fwtest, struct sk_buff *skb, bool udi_header)
{
	u16 vif;

	skb = slsi_skb_copy(skb, GFP_KERNEL);

	if (udi_header)
		skb_pull(skb, sizeof(struct udi_msg_t));

	vif = fapi_get_vif(skb);

	SLSI_DBG3(sdev, SLSI_FW_TEST, "sig:0x%.4X, vif:%d", fapi_get_sigid(skb), vif);
	slsi_debug_frame(sdev, NULL, skb, "PROCESS");

	slsi_skb_work_enqueue(&fwtest->fw_test_work, skb);
}

int slsi_fw_test_signal(struct slsi_dev *sdev, struct slsi_fw_test *fwtest, struct sk_buff *skb)
{
	u16 vif = fapi_get_vif(skb);

	/* Atleast one write to via the UDI interface */
	fwtest->fw_test_enabled = true;
	SLSI_DBG3(sdev, SLSI_FW_TEST, "0x%p: sig:0x%.4X, vif:%d\n", skb, fapi_get_sigid(skb), vif);

	if (WARN(vif > CONFIG_SCSC_WLAN_MAX_INTERFACES, "vif(%d) > CONFIG_SCSC_WLAN_MAX_INTERFACES", vif))
		return -EINVAL;

	switch (fapi_get_sigid(skb)) {
	case MLME_ADD_VIF_REQ:
		SLSI_DBG2(sdev, SLSI_FW_TEST, "0x%p: Save MLME_ADD_VIF_REQ(0x%.4X, vif:%d)\n", skb, fapi_get_sigid(skb), vif);
		slsi_fw_test_save_frame(sdev, fwtest, fwtest->mlme_add_vif_req, skb, false);
		slsi_fw_test_process_frame(sdev, fwtest, skb, false);
		break;
	case MLME_CONNECT_REQ:
		SLSI_DBG2(sdev, SLSI_FW_TEST, "0x%p: Save MLME_CONNECT_REQ(0x%.4X, vif:%d)\n", skb, fapi_get_sigid(skb), vif);
		slsi_fw_test_save_frame(sdev, fwtest, fwtest->mlme_connect_req, skb, false);
		break;
	case MLME_DEL_VIF_REQ:
		SLSI_DBG2(sdev, SLSI_FW_TEST, "0x%p: Save MLME_DEL_VIF_REQ(0x%.4X, vif:%d)\n", skb, fapi_get_sigid(skb), vif);
		slsi_fw_test_process_frame(sdev, fwtest, skb, false);
		break;
	default:
		return 0;
	}

	return 0;
}

int slsi_fw_test_signal_with_udi_header(struct slsi_dev *sdev, struct slsi_fw_test *fwtest, struct sk_buff *skb)
{
	struct udi_msg_t          *udi_msg = (struct udi_msg_t *)skb->data;
	struct fapi_vif_signal_header *fapi_header = (struct fapi_vif_signal_header *)(skb->data + sizeof(struct udi_msg_t));

	if (!fwtest->fw_test_enabled)
		return 0;

	SLSI_DBG3(sdev, SLSI_FW_TEST, "0x%p: sig:0x%.4X, vif:%d\n", skb, le16_to_cpu(fapi_header->id), le16_to_cpu(fapi_header->vif));

	if (udi_msg->direction == SLSI_LOG_DIRECTION_TO_HOST) {
		switch (le16_to_cpu(fapi_header->id)) {
		case MLME_DISCONNECT_IND:
			SLSI_DBG2(sdev, SLSI_FW_TEST, "0x%p: Process MLME_DISCONNECT_IND(0x%.4X, vif:%d)\n", skb, le16_to_cpu(fapi_header->id), le16_to_cpu(fapi_header->vif));
			slsi_fw_test_process_frame(sdev, fwtest, skb, true);
			break;
		case MLME_DISCONNECTED_IND:
			SLSI_DBG2(sdev, SLSI_FW_TEST, "0x%p: Process MLME_DISCONNECTED_IND(0x%.4X, vif:%d)\n", skb, le16_to_cpu(fapi_header->id), le16_to_cpu(fapi_header->vif));
			slsi_fw_test_process_frame(sdev, fwtest, skb, true);
			break;
		case MLME_CONNECT_IND:
			SLSI_DBG2(sdev, SLSI_FW_TEST, "0x%p: Process MLME_CONNECT_IND(0x%.4X, vif:%d)\n", skb, le16_to_cpu(fapi_header->id), le16_to_cpu(fapi_header->vif));
			slsi_fw_test_process_frame(sdev, fwtest, skb, true);
			break;
		case MLME_CONNECTED_IND:
			SLSI_DBG2(sdev, SLSI_FW_TEST, "0x%p: Process MLME_CONNECTED_IND(0x%.4X, vif:%d)\n", skb, le16_to_cpu(fapi_header->id), le16_to_cpu(fapi_header->vif));
			slsi_fw_test_process_frame(sdev, fwtest, skb, true);
			break;
		case MLME_ROAMED_IND:
			SLSI_DBG2(sdev, SLSI_FW_TEST, "0x%p: Process MLME_ROAMED_IND(0x%.4X, vif:%d)\n", skb, le16_to_cpu(fapi_header->id), le16_to_cpu(fapi_header->vif));
			slsi_fw_test_process_frame(sdev, fwtest, skb, true);
			break;
		case MLME_CONNECT_CFM:
			SLSI_DBG2(sdev, SLSI_FW_TEST, "0x%p: Save MLME_CONNECT_CFM(0x%.4X, vif:%d)\n", skb, le16_to_cpu(fapi_header->id), le16_to_cpu(fapi_header->vif));
			slsi_fw_test_save_frame(sdev, fwtest, fwtest->mlme_connect_cfm, skb, true);
			break;
		case MLME_PROCEDURE_STARTED_IND:
			SLSI_DBG2(sdev, SLSI_FW_TEST, "0x%p: Save MLME_PROCEDURE_STARTED_IND(0x%.4X, vif:%d)\n", skb, le16_to_cpu(fapi_header->id), le16_to_cpu(fapi_header->vif));
			slsi_fw_test_save_frame(sdev, fwtest, fwtest->mlme_procedure_started_ind, skb, true);
			SLSI_DBG2(sdev, SLSI_FW_TEST, "0x%p: Process MLME_PROCEDURE_STARTED_IND(0x%.4X, vif:%d)\n", skb, le16_to_cpu(fapi_header->id), le16_to_cpu(fapi_header->vif));
			slsi_fw_test_process_frame(sdev, fwtest, skb, true);
			break;
		case MLME_START_CFM:
			SLSI_DBG2(sdev, SLSI_FW_TEST, "0x%p: Process MLME_START_CFM(0x%.4X, vif:%d)\n", skb, le16_to_cpu(fapi_header->id), le16_to_cpu(fapi_header->vif));
			slsi_fw_test_process_frame(sdev, fwtest, skb, true);
			sdev->device_config.ap_disconnect_ind_timeout =  SLSI_DEFAULT_AP_DISCONNECT_IND_TIMEOUT;
			break;
		default:
			break;
		}
	}

	return 0;
}

static void slsi_fw_test_connect_station_roam(struct slsi_dev *sdev, struct net_device *dev, struct slsi_fw_test *fwtest, struct sk_buff *skb)
{
	struct netdev_vif     *ndev_vif = netdev_priv(dev);
	struct slsi_peer      *peer = slsi_get_peer_from_qs(sdev, dev, SLSI_STA_PEER_QUEUESET);
	struct ieee80211_mgmt *mgmt = fapi_get_mgmt(skb);
	struct sk_buff        *mlme_procedure_started_ind;

	WARN_ON(!SLSI_MUTEX_IS_LOCKED(ndev_vif->vif_mutex));

	SLSI_NET_DBG1(dev, SLSI_FW_TEST, "Station Connect(vif:%d) Roam\n", ndev_vif->ifnum);

	if (WARN(!ndev_vif->is_fw_test, "!is_fw_test"))
		return;

	if (WARN(!ndev_vif->activated, "Not Activated"))
		return;

	if (WARN(ndev_vif->vif_type != FAPI_VIFTYPE_STATION, "Not Station Vif"))
		return;

	if (WARN(!peer, "peer not found"))
		return;

	slsi_spinlock_lock(&fwtest->fw_test_lock);
	mlme_procedure_started_ind = fwtest->mlme_procedure_started_ind[ndev_vif->ifnum];
	fwtest->mlme_procedure_started_ind[ndev_vif->ifnum] = NULL;
	slsi_spinlock_unlock(&fwtest->fw_test_lock);

	if (WARN(!mlme_procedure_started_ind, "mlme_procedure_started_ind not found"))
		return;

	slsi_rx_ba_stop_all(dev, peer);

	SLSI_ETHER_COPY(peer->address, mgmt->bssid);
	slsi_peer_update_assoc_req(sdev, dev, peer, mlme_procedure_started_ind);
	slsi_peer_update_assoc_rsp(sdev, dev, peer, slsi_skb_copy(skb, GFP_KERNEL));
}

static void slsi_fw_test_connect_start_station(struct slsi_dev *sdev, struct net_device *dev, struct slsi_fw_test *fwtest, struct sk_buff *skb)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct sk_buff    *req;
	struct sk_buff    *cfm;
	struct sk_buff    *ind;
	struct slsi_peer  *peer;
	u8                bssid[ETH_ALEN];

	WARN_ON(!SLSI_MUTEX_IS_LOCKED(ndev_vif->vif_mutex));

	SLSI_NET_DBG1(dev, SLSI_FW_TEST, "Station Connect Start(vif:%d)\n", ndev_vif->ifnum);

	if (WARN(!ndev_vif->is_fw_test, "!is_fw_test"))
		return;
	if (WARN(ndev_vif->activated, "Already Activated"))
		return;

	slsi_spinlock_lock(&fwtest->fw_test_lock);
	req = fwtest->mlme_connect_req[ndev_vif->ifnum];
	cfm = fwtest->mlme_connect_cfm[ndev_vif->ifnum];
	ind = fwtest->mlme_procedure_started_ind[ndev_vif->ifnum];
	if (req)
		SLSI_ETHER_COPY(bssid, fapi_get_buff(req, u.mlme_connect_req.bssid));
	slsi_spinlock_unlock(&fwtest->fw_test_lock);

	if (WARN(!req, "mlme_connect_req Not found"))
		return;
	if (WARN(!cfm, "mlme_connect_cfm Not found"))
		return;

	ndev_vif->iftype = NL80211_IFTYPE_STATION;
	dev->ieee80211_ptr->iftype = NL80211_IFTYPE_STATION;
	ndev_vif->vif_type = FAPI_VIFTYPE_STATION;

	SLSI_NET_DBG1(dev, SLSI_FW_TEST, "vif:%d slsi_vif_activated", ndev_vif->ifnum);
	if (WARN(slsi_vif_activated(sdev, dev) != 0, "slsi_vif_activated() Failed"))
		return;

	peer = slsi_peer_add(sdev, dev, bssid, SLSI_STA_PEER_QUEUESET + 1);
	if (WARN(!peer, "slsi_peer_add(%pM) Failed", bssid)) {
		slsi_vif_deactivated(sdev, dev);
		return;
	}

	slsi_peer_update_assoc_req(sdev, dev, peer, slsi_skb_copy(skb, GFP_KERNEL));
}

static void slsi_fw_test_connect_station(struct slsi_dev *sdev, struct net_device *dev, struct slsi_fw_test *fwtest, struct sk_buff *skb)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct sk_buff    *req;
	struct sk_buff    *cfm;
	struct sk_buff    *ind;
	struct slsi_peer  *peer;
	u16               result;

	WARN_ON(!SLSI_MUTEX_IS_LOCKED(ndev_vif->vif_mutex));

	SLSI_NET_DBG1(dev, SLSI_FW_TEST, "Station Connect(vif:%d)\n", ndev_vif->ifnum);

	if (WARN(!ndev_vif->is_fw_test, "!is_fw_test"))
		return;

	result = fapi_get_u16(skb, u.mlme_connect_ind.result_code);

	slsi_spinlock_lock(&fwtest->fw_test_lock);
	req = fwtest->mlme_connect_req[ndev_vif->ifnum];
	cfm = fwtest->mlme_connect_cfm[ndev_vif->ifnum];
	ind = fwtest->mlme_procedure_started_ind[ndev_vif->ifnum];
	fwtest->mlme_connect_req[ndev_vif->ifnum] = NULL;
	fwtest->mlme_connect_cfm[ndev_vif->ifnum] = NULL;
	fwtest->mlme_procedure_started_ind[ndev_vif->ifnum] = NULL;
	slsi_spinlock_unlock(&fwtest->fw_test_lock);

	if (WARN(!req, "mlme_connect_req Not found"))
		goto exit;
	if (WARN(!cfm, "mlme_connect_cfm Not found"))
		goto exit;
	if (FAPI_RESULTCODE_SUCCESS == result &&
	    WARN(!ind, "mlme_procedure_started_ind Not found"))
		goto exit;
	if (FAPI_RESULTCODE_SUCCESS != result)
		goto exit;

	if (WARN(!ndev_vif->activated, "Not Activated"))
		return;

	peer = slsi_get_peer_from_mac(sdev, dev, fapi_get_buff(req, u.mlme_connect_req.bssid));
	if (WARN(!peer, "slsi_get_peer_from_mac(%pM) Failed", fapi_get_buff(req, u.mlme_connect_req.bssid)))
		goto exit;

	slsi_ps_port_control(sdev, dev, peer, SLSI_STA_CONN_STATE_CONNECTED);
	netif_carrier_on(dev);

exit:
	slsi_kfree_skb(req);
	slsi_kfree_skb(cfm);
	slsi_kfree_skb(ind);
}

static void slsi_fw_test_started_network(struct slsi_dev *sdev, struct net_device *dev, struct slsi_fw_test *fwtest, struct sk_buff *skb)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	u16               result = fapi_get_u16(skb, u.mlme_start_cfm.result_code);

	SLSI_UNUSED_PARAMETER(fwtest);

	WARN_ON(!SLSI_MUTEX_IS_LOCKED(ndev_vif->vif_mutex));

	SLSI_NET_DBG1(dev, SLSI_FW_TEST, "Start Network(vif:%d)\n", ndev_vif->ifnum);

	if (WARN(!ndev_vif->is_fw_test, "!is_fw_test"))
		return;
	if (WARN(ndev_vif->activated, "Already Activated"))
		return;

	ndev_vif->iftype = NL80211_IFTYPE_AP;
	dev->ieee80211_ptr->iftype = NL80211_IFTYPE_AP;
	ndev_vif->vif_type = FAPI_VIFTYPE_AP;

	if (WARN(slsi_vif_activated(sdev, dev) != 0, "slsi_vif_activated() Failed"))
		return;

	if (FAPI_RESULTCODE_SUCCESS == result)
		netif_carrier_on(dev);
}

static void slsi_fw_test_stop_network(struct slsi_dev *sdev, struct net_device *dev, struct slsi_fw_test *fwtest, struct sk_buff *skb)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);

	SLSI_UNUSED_PARAMETER(fwtest);
	SLSI_UNUSED_PARAMETER(skb);

	WARN_ON(!SLSI_MUTEX_IS_LOCKED(ndev_vif->vif_mutex));

	if (!ndev_vif->is_fw_test)
		return;

	SLSI_NET_DBG1(dev, SLSI_FW_TEST, "Stopping Network(vif:%d)\n", ndev_vif->ifnum);

	if (WARN(!ndev_vif->activated, "Not Activated"))
		return;

	netif_carrier_off(dev);
	slsi_vif_deactivated(sdev, dev);
}

static void slsi_fw_test_started_ibss_network(struct slsi_dev *sdev, struct net_device *dev, struct slsi_fw_test *fwtest, struct sk_buff *skb)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	u16               result = fapi_get_u16(skb, u.mlme_start_cfm.result_code);

	WARN_ON(!SLSI_MUTEX_IS_LOCKED(ndev_vif->vif_mutex));

	SLSI_NET_DBG1(dev, SLSI_FW_TEST, "Start Network(vif:%d)\n", ndev_vif->ifnum);

	if (WARN(!ndev_vif->is_fw_test, "!is_fw_test"))
		return;
	if (WARN(ndev_vif->activated, "Already Activated"))
		return;

	if (FAPI_RESULTCODE_SUCCESS != result) {
		SLSI_NET_ERR(dev, "slsi_fw_test_started_ibss_network:  start_cfm returned failure (rc=%d)\n", result);
		return;
	}

	/* Set iftype here to be sure the right actions are taken in slsi_vif_activated().
	 * slsi_vif_activated() does a bit of checking and then sets up the packet scheduler
	 * according to the iftype.
	 * There is no neutral setting for iftype - it is set explicitly and not
	 * checked elsehwere for a previous value.
	 */
	ndev_vif->iftype = NL80211_IFTYPE_ADHOC;
	dev->ieee80211_ptr->iftype = NL80211_IFTYPE_ADHOC;
	ndev_vif->vif_type = FAPI_VIFTYPE_ADHOC;

	if (WARN(slsi_vif_activated(sdev, dev) != 0, "slsi_vif_activated() Failed"))
		return;

	/* Now we have everything connected for this vif, allow transmit traffic. */

	netif_carrier_on(dev);
}

static void slsi_fw_test_connect_start_ap(struct slsi_dev *sdev, struct net_device *dev, struct slsi_fw_test *fwtest, struct sk_buff *skb)
{
	struct netdev_vif     *ndev_vif = netdev_priv(dev);
	struct slsi_peer      *peer = NULL;
	struct ieee80211_mgmt *mgmt = fapi_get_mgmt(skb);
	u16                   peer_index;

	SLSI_UNUSED_PARAMETER(fwtest);

	WARN_ON(!SLSI_MUTEX_IS_LOCKED(ndev_vif->vif_mutex));

	SLSI_NET_DBG1(dev, SLSI_FW_TEST, "Network Peer Connect Start(vif:%d)\n", ndev_vif->ifnum);
	WARN(!ndev_vif->is_fw_test, "!is_fw_test");

	if (WARN(!ndev_vif->activated, "Not Activated"))
		return;

	if (WARN_ON(!ieee80211_is_assoc_req(mgmt->frame_control) &&
		    !ieee80211_is_reassoc_req(mgmt->frame_control)))
		return;
	peer_index = fapi_get_u16(skb, u.mlme_procedure_started_ind.peer_index);

	peer = slsi_peer_add(sdev, dev, mgmt->sa, peer_index);
	if (WARN_ON(!peer))
		return;

	slsi_peer_update_assoc_req(sdev, dev, peer, slsi_skb_copy(skb, GFP_KERNEL));
	peer->connected_state = SLSI_STA_CONN_STATE_CONNECTING;
}

static void slsi_fw_test_connected_network(struct slsi_dev *sdev, struct net_device *dev, struct slsi_fw_test *fwtest, struct sk_buff *skb)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct slsi_peer  *peer = NULL;
	u16               peer_index = fapi_get_u16(skb, u.mlme_connected_ind.peer_index);

	SLSI_UNUSED_PARAMETER(fwtest);

	WARN_ON(!SLSI_MUTEX_IS_LOCKED(ndev_vif->vif_mutex));

	SLSI_NET_DBG1(dev, SLSI_FW_TEST, "Network Peer Connect(vif:%d, peer_index:%d)\n", ndev_vif->ifnum, peer_index);
	WARN(!ndev_vif->is_fw_test, "!is_fw_test");

	if (WARN(!ndev_vif->activated, "Not Activated"))
		return;

	if (WARN_ON(peer_index > SLSI_PEER_INDEX_MAX))
		return;

	peer = slsi_get_peer_from_qs(sdev, dev, peer_index - 1);
	if (WARN(!peer, "Peer(peer_index:%d) Not Found", peer_index))
		return;

	slsi_ps_port_control(sdev, dev, peer, SLSI_STA_CONN_STATE_CONNECTED);
	peer->connected_state = SLSI_STA_CONN_STATE_CONNECTED;

	slsi_rx_buffered_frames(sdev, dev, peer);
}

static void slsi_fw_test_connected_ibss_network(struct slsi_dev *sdev, struct net_device *dev, struct slsi_fw_test *fwtest, struct sk_buff *skb)
{
	struct netdev_vif     *ndev_vif = netdev_priv(dev);
	struct slsi_peer      *peer = NULL;
	u16                   peer_index = fapi_get_u16(skb, u.mlme_connected_ind.peer_index);
	struct ieee80211_mgmt *mgmt = fapi_get_mgmt(skb);

	WARN_ON(!SLSI_MUTEX_IS_LOCKED(ndev_vif->vif_mutex));

	SLSI_NET_DBG1(dev, SLSI_FW_TEST, "Network Peer Connect(vif:%d, peer_index:%d)\n", ndev_vif->ifnum, peer_index);
	WARN(!ndev_vif->is_fw_test, "!is_fw_test");

	if (WARN(!ndev_vif->activated, "Not Activated"))
		return;

	peer = slsi_peer_add(sdev, dev, mgmt->sa, peer_index);
	if (WARN_ON(!peer))
		return;

	slsi_ps_port_control(sdev, dev, peer, SLSI_STA_CONN_STATE_CONNECTED);
	peer->connected_state = SLSI_STA_CONN_STATE_CONNECTED;

	slsi_rx_buffered_frames(sdev, dev, peer);
}

/* Setup the NetDev / Peers based on the saved frames */
static void slsi_fw_test_procedure_started_ind(struct slsi_dev *sdev, struct net_device *dev, struct slsi_fw_test *fwtest, struct sk_buff *skb)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct sk_buff    *add_vif_req;
	u16               viftype = FAPI_VIFTYPE_STATION;

	if (!ndev_vif->is_fw_test) {
		slsi_kfree_skb(skb);
		return;
	}

	SLSI_NET_DBG1(dev, SLSI_FW_TEST, "ProceedureStarted(vif:%d)\n", ndev_vif->ifnum);

	if (fapi_get_u16(skb, u.mlme_procedure_started_ind.procedure_type) != FAPI_PROCEDURETYPE_CONNECTION_STARTED) {
		slsi_kfree_skb(skb);
		return;
	}

	/* Set up the VIF and Data plane ready to go BUT do not open the control port */
	slsi_spinlock_lock(&fwtest->fw_test_lock);
	add_vif_req = fwtest->mlme_add_vif_req[ndev_vif->ifnum];
	if (add_vif_req)
		viftype = fapi_get_u16(add_vif_req, u.mlme_add_vif_req.virtual_interface_type);
	slsi_spinlock_unlock(&fwtest->fw_test_lock);

	SLSI_NET_DBG1(dev, SLSI_FW_TEST, "Start UDI test NetDevice(vif:%d)\n", ndev_vif->ifnum);
	if (WARN(!add_vif_req, "fwtest->mlme_add_vif_req[ndev_vif->ifnum] == NULL"))
		goto out;

	SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);

	switch (viftype) {
	case FAPI_VIFTYPE_STATION:
		slsi_fw_test_connect_start_station(sdev, dev, fwtest, skb);
		break;
	case FAPI_VIFTYPE_AP:
		slsi_fw_test_connect_start_ap(sdev, dev, fwtest, skb);
		break;
	default:
		SLSI_NET_DBG1(dev, SLSI_FW_TEST, "vif:%d virtual_interface_type:%d NOT SUPPORTED", ndev_vif->ifnum, viftype);
		break;
	}

	SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);

out:
	slsi_kfree_skb(skb);
}

/* Setup the NetDev / Peers based on the saved frames */
static void slsi_fw_test_connect_ind(struct slsi_dev *sdev, struct net_device *dev, struct slsi_fw_test *fwtest, struct sk_buff *skb)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct sk_buff    *add_vif_req;
	u16               viftype = FAPI_VIFTYPE_STATION;

	if (!ndev_vif->is_fw_test) {
		slsi_kfree_skb(skb);
		return;
	}

	SLSI_NET_DBG1(dev, SLSI_FW_TEST, "Network Peer Connect(vif:%d)\n", ndev_vif->ifnum);

	slsi_spinlock_lock(&fwtest->fw_test_lock);
	add_vif_req = fwtest->mlme_add_vif_req[ndev_vif->ifnum];
	if (add_vif_req)
		viftype = fapi_get_u16(add_vif_req, u.mlme_add_vif_req.virtual_interface_type);
	slsi_spinlock_unlock(&fwtest->fw_test_lock);

	SLSI_NET_DBG1(dev, SLSI_FW_TEST, "Start UDI test NetDevice(vif:%d)\n", ndev_vif->ifnum);
	if (WARN(!add_vif_req, "fwtest->mlme_add_vif_req[ndev_vif->ifnum] == NULL"))
		goto out;

	SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);

	switch (viftype) {
	case FAPI_VIFTYPE_STATION:
		slsi_fw_test_connect_station(sdev, dev, fwtest, skb);
		break;
	default:
		SLSI_NET_DBG1(dev, SLSI_FW_TEST, "vif:%d virtual_interface_type:%d NOT SUPPORTED", ndev_vif->ifnum, viftype);
		break;
	}

	SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);

out:
	slsi_kfree_skb(skb);
}

static void slsi_fw_test_connected_ind(struct slsi_dev *sdev, struct net_device *dev, struct slsi_fw_test *fwtest, struct sk_buff *skb)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct sk_buff    *add_vif_req;
	u16               viftype = FAPI_VIFTYPE_STATION;

	if (!ndev_vif->is_fw_test) {
		slsi_kfree_skb(skb);
		return;
	}

	SLSI_NET_DBG1(dev, SLSI_FW_TEST, "Connected(vif:%d)\n", ndev_vif->ifnum);

	slsi_spinlock_lock(&fwtest->fw_test_lock);
	add_vif_req = fwtest->mlme_add_vif_req[ndev_vif->ifnum];
	if (add_vif_req)
		viftype = fapi_get_u16(add_vif_req, u.mlme_add_vif_req.virtual_interface_type);
	slsi_spinlock_unlock(&fwtest->fw_test_lock);

	if (WARN(!add_vif_req, "fwtest->mlme_add_vif_req[ndev_vif->ifnum] == NULL"))
		goto out;

	SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);

	switch (viftype) {
	case FAPI_VIFTYPE_ADHOC:
		slsi_fw_test_connected_ibss_network(sdev, dev, fwtest, skb);
		break;
	case FAPI_VIFTYPE_AP:
		slsi_fw_test_connected_network(sdev, dev, fwtest, skb);
		break;
	default:
		SLSI_NET_DBG1(dev, SLSI_FW_TEST, "vif:%d virtual_interface_type:%d NOT SUPPORTED", ndev_vif->ifnum, viftype);
		break;
	}

	SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);

out:
	slsi_kfree_skb(skb);
}

static void slsi_fw_test_roamed_ind(struct slsi_dev *sdev, struct net_device *dev, struct slsi_fw_test *fwtest, struct sk_buff *skb)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct sk_buff    *add_vif_req;
	u16               viftype = FAPI_VIFTYPE_STATION;

	if (!ndev_vif->is_fw_test) {
		slsi_kfree_skb(skb);
		return;
	}

	SLSI_NET_DBG1(dev, SLSI_FW_TEST, "Roamed(vif:%d)\n", ndev_vif->ifnum);

	slsi_spinlock_lock(&fwtest->fw_test_lock);
	add_vif_req = fwtest->mlme_add_vif_req[ndev_vif->ifnum];
	if (add_vif_req)
		viftype = fapi_get_u16(add_vif_req, u.mlme_add_vif_req.virtual_interface_type);
	slsi_spinlock_unlock(&fwtest->fw_test_lock);

	if (WARN(!add_vif_req, "fwtest->mlme_add_vif_req[ndev_vif->ifnum] == NULL"))
		goto out;

	SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);

	switch (viftype) {
	case FAPI_VIFTYPE_STATION:
		slsi_fw_test_connect_station_roam(sdev, dev, fwtest, skb);
		break;
	default:
		SLSI_NET_DBG1(dev, SLSI_FW_TEST, "vif:%d virtual_interface_type:%d NOT SUPPORTED", ndev_vif->ifnum, viftype);
		break;
	}

	SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);

out:
	slsi_kfree_skb(skb);
}

static void slsi_fw_test_disconnect_station(struct slsi_dev *sdev, struct net_device *dev, struct slsi_fw_test *fwtest, struct sk_buff *skb)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct slsi_peer  *peer = slsi_get_peer_from_qs(sdev, dev, SLSI_STA_PEER_QUEUESET);

	SLSI_UNUSED_PARAMETER(fwtest);
	SLSI_UNUSED_PARAMETER(skb);

	WARN_ON(!SLSI_MUTEX_IS_LOCKED(ndev_vif->vif_mutex));

	if (!ndev_vif->is_fw_test)
		return;

	SLSI_NET_DBG1(dev, SLSI_FW_TEST, "Station Disconnect(vif:%d)\n", ndev_vif->ifnum);

	if (WARN(!ndev_vif->activated, "Not Activated"))
		return;

	netif_carrier_off(dev);
	if (peer)
		slsi_peer_remove(sdev, dev, peer);
	slsi_vif_deactivated(sdev, dev);
}

static void slsi_fw_test_disconnect_network(struct slsi_dev *sdev, struct net_device *dev, struct slsi_fw_test *fwtest, struct sk_buff *skb)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	/* Find the peer based on MAC address, mlme-disconnect-ind and mlme-disconnected-ind
	 * both have the MAC address in the same position.
	 */
	struct slsi_peer *peer = slsi_get_peer_from_mac(sdev, dev, fapi_get_buff(skb, u.mlme_disconnect_ind.peer_sta_address));

	SLSI_UNUSED_PARAMETER(fwtest);

	WARN_ON(!SLSI_MUTEX_IS_LOCKED(ndev_vif->vif_mutex));

	if (!ndev_vif->is_fw_test)
		return;

	SLSI_NET_DBG1(dev, SLSI_FW_TEST, "Network Peer Disconnect(vif:%d)\n", ndev_vif->ifnum);

	if (peer)
		slsi_peer_remove(sdev, dev, peer);
}

static void slsi_fw_test_disconnected_ind(struct slsi_dev *sdev, struct net_device *dev, struct slsi_fw_test *fwtest, struct sk_buff *skb)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct sk_buff    *add_vif_req;
	u16               viftype = FAPI_VIFTYPE_STATION;

	if (!ndev_vif->is_fw_test) {
		slsi_kfree_skb(skb);
		return;
	}

	slsi_spinlock_lock(&fwtest->fw_test_lock);
	add_vif_req = fwtest->mlme_add_vif_req[ndev_vif->ifnum];
	if (add_vif_req)
		viftype = fapi_get_u16(add_vif_req, u.mlme_add_vif_req.virtual_interface_type);
	slsi_spinlock_unlock(&fwtest->fw_test_lock);

	if (WARN(!add_vif_req, "fwtest->mlme_add_vif_req[ndev_vif->ifnum] == NULL"))
		goto out;

	SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);

	switch (viftype) {
	case FAPI_VIFTYPE_STATION:
		slsi_fw_test_disconnect_station(sdev, dev, fwtest, skb);
		break;
	/* Assume handling for AP and ADHOC are the same */
	case FAPI_VIFTYPE_AP:
	case FAPI_VIFTYPE_ADHOC:
		slsi_fw_test_disconnect_network(sdev, dev, fwtest, skb);
		break;
	default:
		SLSI_NET_DBG1(dev, SLSI_FW_TEST, "vif:%d virtual_interface_type:%d NOT SUPPORTED", ndev_vif->ifnum, viftype);
		break;
	}
	SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);

out:
	slsi_kfree_skb(skb);
}

/* Setup the NetDev */
static void slsi_fw_test_start_cfm(struct slsi_dev *sdev, struct net_device *dev, struct slsi_fw_test *fwtest, struct sk_buff *skb)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct sk_buff    *add_vif_req;
	u16               viftype = FAPI_VIFTYPE_UNSYNCHRONISED;

	if (!ndev_vif->is_fw_test) {
		slsi_kfree_skb(skb);
		return;
	}

	SLSI_NET_DBG1(dev, SLSI_FW_TEST, "Network Start(vif:%d)\n", ndev_vif->ifnum);

	slsi_spinlock_lock(&fwtest->fw_test_lock);
	add_vif_req = fwtest->mlme_add_vif_req[ndev_vif->ifnum];
	if (add_vif_req)
		viftype = fapi_get_u16(add_vif_req, u.mlme_add_vif_req.virtual_interface_type);
	slsi_spinlock_unlock(&fwtest->fw_test_lock);

	SLSI_NET_DBG1(dev, SLSI_FW_TEST, "Start UDI test NetDevice(vif:%d)\n", ndev_vif->ifnum);
	if (WARN(!add_vif_req, "fwtest->mlme_add_vif_req[ndev_vif->ifnum] == NULL"))
		goto out;

	SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);

	switch (viftype) {
	/* Assume the AP and ADHOC can be treated the same */
	case FAPI_VIFTYPE_ADHOC:
		slsi_fw_test_started_ibss_network(sdev, dev, fwtest, skb);
		break;
	case FAPI_VIFTYPE_AP:
		slsi_fw_test_started_network(sdev, dev, fwtest, skb);
		break;
	default:
		SLSI_NET_DBG1(dev, SLSI_FW_TEST, "vif:%d virtual_interface_type:%d NOT SUPPORTED", ndev_vif->ifnum, viftype);
		break;
	}

	SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);

out:
	slsi_kfree_skb(skb);
}

static void slsi_fw_test_add_vif_req(struct slsi_dev *sdev, struct net_device *dev, struct slsi_fw_test *fwtest, struct sk_buff *skb)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);

	SLSI_UNUSED_PARAMETER(sdev);
	SLSI_UNUSED_PARAMETER(fwtest);

	SLSI_DBG1(sdev, SLSI_FW_TEST, "Mark UDI test NetDevice(vif:%d)\n", fapi_get_vif(skb));
	ndev_vif->is_fw_test = true;
	slsi_kfree_skb(skb);
}

static void slsi_fw_test_del_vif_req(struct slsi_dev *sdev, struct net_device *dev, struct slsi_fw_test *fwtest, struct sk_buff *skb)
{
	struct netdev_vif *ndev_vif = netdev_priv(dev);
	struct sk_buff    *add_vif_req;
	u16               viftype = FAPI_VIFTYPE_UNSYNCHRONISED;

	SLSI_DBG1(sdev, SLSI_FW_TEST, "Unmark UDI test NetDevice(vif:%d)\n", fapi_get_vif(skb));

	slsi_spinlock_lock(&fwtest->fw_test_lock);
	add_vif_req = fwtest->mlme_add_vif_req[ndev_vif->ifnum];
	if (add_vif_req)
		viftype = fapi_get_u16(add_vif_req, u.mlme_add_vif_req.virtual_interface_type);
	slsi_kfree_skb(fwtest->mlme_add_vif_req[ndev_vif->ifnum]);
	slsi_kfree_skb(fwtest->mlme_connect_req[ndev_vif->ifnum]);
	slsi_kfree_skb(fwtest->mlme_connect_cfm[ndev_vif->ifnum]);
	slsi_kfree_skb(fwtest->mlme_procedure_started_ind[ndev_vif->ifnum]);

	fwtest->mlme_add_vif_req[ndev_vif->ifnum] = NULL;
	fwtest->mlme_connect_req[ndev_vif->ifnum] = NULL;
	fwtest->mlme_connect_cfm[ndev_vif->ifnum] = NULL;
	fwtest->mlme_procedure_started_ind[ndev_vif->ifnum] = NULL;
	slsi_spinlock_unlock(&fwtest->fw_test_lock);

	SLSI_MUTEX_LOCK(ndev_vif->vif_mutex);

	switch (viftype) {
	/* As there is no specific MLME primitive for shutting down the network
	 * perform an actions on the MLME-DEL-VIF.
	 * Assume the AP and ADHOC can be treated the same.
	 */
	case FAPI_VIFTYPE_ADHOC:
	case FAPI_VIFTYPE_AP:
		slsi_fw_test_stop_network(sdev, dev, fwtest, skb);
		break;
	default:
		if (ndev_vif->is_fw_test && ndev_vif->activated) {
			netif_carrier_off(dev);
			slsi_vif_deactivated(sdev, dev);
		}
		break;
	}
	ndev_vif->is_fw_test = false;

	SLSI_MUTEX_UNLOCK(ndev_vif->vif_mutex);

	slsi_kfree_skb(skb);
}

void slsi_fw_test_work(struct work_struct *work)
{
	struct slsi_fw_test *fw_test = container_of(work, struct slsi_fw_test, fw_test_work.work);
	struct slsi_dev     *sdev = fw_test->sdev;
	struct sk_buff      *skb = slsi_skb_work_dequeue(&fw_test->fw_test_work);
	struct net_device   *dev;

	while (skb) {
		u16 vif = fapi_get_vif(skb);

		SLSI_DBG3(sdev, SLSI_FW_TEST, "0x%p: Signal:0x%.4X, vif:%d\n", skb, fapi_get_sigid(skb), vif);

		if (WARN(!vif, "!vif")) {
			slsi_kfree_skb(skb);
			skb = slsi_skb_work_dequeue(&fw_test->fw_test_work);
			continue;
		}

		SLSI_MUTEX_LOCK(sdev->netdev_add_remove_mutex);
		dev = slsi_get_netdev_locked(sdev, vif);
		if (!dev) {
			/* Just ignore the signal. This is valid in some error testing scenarios*/
			SLSI_MUTEX_UNLOCK(sdev->netdev_add_remove_mutex);
			slsi_kfree_skb(skb);
			skb = slsi_skb_work_dequeue(&fw_test->fw_test_work);
			continue;
		}

		switch (fapi_get_sigid(skb)) {
		case MLME_PROCEDURE_STARTED_IND:
			slsi_fw_test_procedure_started_ind(sdev, dev, fw_test, skb);
			break;
		case MLME_CONNECT_IND:
			slsi_fw_test_connect_ind(sdev, dev, fw_test, skb);
			break;
		case MLME_ROAMED_IND:
			slsi_fw_test_roamed_ind(sdev, dev, fw_test, skb);
			break;
		case MLME_CONNECTED_IND:
			slsi_fw_test_connected_ind(sdev, dev, fw_test, skb);
			break;
		case MLME_DISCONNECT_IND:
		case MLME_DISCONNECTED_IND:
			slsi_fw_test_disconnected_ind(sdev, dev, fw_test, skb);
			break;
		case MLME_START_CFM:
			slsi_fw_test_start_cfm(sdev, dev, fw_test, skb);
			break;
		case MLME_ADD_VIF_REQ:
			slsi_fw_test_add_vif_req(sdev, dev, fw_test, skb);
			break;
		case MLME_DEL_VIF_REQ:
			slsi_fw_test_del_vif_req(sdev, dev, fw_test, skb);
			break;
		default:
			WARN(1, "Unhandled Signal");
			slsi_kfree_skb(skb);
			break;
		}
		SLSI_MUTEX_UNLOCK(sdev->netdev_add_remove_mutex);

		skb = slsi_skb_work_dequeue(&fw_test->fw_test_work);
	}
}

void slsi_fw_test_init(struct slsi_dev *sdev, struct slsi_fw_test *fwtest)
{
	SLSI_DBG1(sdev, SLSI_FW_TEST, "\n");
	memset(fwtest, 0x00, sizeof(struct slsi_fw_test));
	fwtest->sdev = sdev;
	slsi_spinlock_create(&fwtest->fw_test_lock);
	slsi_skb_work_init(sdev, NULL, &fwtest->fw_test_work, "slsi_wlan_fw_test", slsi_fw_test_work);
}

void slsi_fw_test_deinit(struct slsi_dev *sdev, struct slsi_fw_test *fwtest)
{
	int i;

	SLSI_UNUSED_PARAMETER(sdev);

	SLSI_DBG1(sdev, SLSI_FW_TEST, "\n");
	fwtest->fw_test_enabled = false;
	slsi_skb_work_deinit(&fwtest->fw_test_work);
	slsi_spinlock_lock(&fwtest->fw_test_lock);
	for (i = 1; i <= CONFIG_SCSC_WLAN_MAX_INTERFACES; i++) {
		slsi_kfree_skb(fwtest->mlme_add_vif_req[i]);
		slsi_kfree_skb(fwtest->mlme_connect_req[i]);
		slsi_kfree_skb(fwtest->mlme_connect_cfm[i]);
		slsi_kfree_skb(fwtest->mlme_procedure_started_ind[i]);

		fwtest->mlme_add_vif_req[i] = NULL;
		fwtest->mlme_connect_req[i] = NULL;
		fwtest->mlme_connect_cfm[i] = NULL;
		fwtest->mlme_procedure_started_ind[i] = NULL;
	}
	slsi_spinlock_unlock(&fwtest->fw_test_lock);
	memset(fwtest, 0x00, sizeof(struct slsi_fw_test));
}
