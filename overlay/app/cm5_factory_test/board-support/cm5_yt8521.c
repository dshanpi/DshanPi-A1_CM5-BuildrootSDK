// SPDX-License-Identifier: GPL-2.0-only
/* UTP/RGMII support for the CM5 baseboard's YT8521 (ID 0000:011a).
 * The vendor 6.1 driver omits this ID. Delay encodings follow the upstream
 * Motorcomm driver: 150ps fine steps plus the 1900ps RX coarse delay.
 */
#include <linux/module.h>
#include <linux/of.h>
#include <linux/phy.h>
#define CM5_YT8521_ID 0x0000011a

static int cm5_ext_change(struct phy_device *phy, u16 reg, u16 mask, u16 bits)
{
 int saved, old, ret, restore;
 phy_lock_mdio_bus(phy);
 saved=__phy_read(phy,0x1e);
 if(saved<0){ret=saved;goto unlock;}
 ret=__phy_write(phy,0x1e,reg);
 if(ret<0)goto restore;
 old=__phy_read(phy,0x1f);
 ret=old<0 ? old : __phy_write(phy,0x1f,(old & ~mask) | (bits & mask));
restore:
 restore=__phy_write(phy,0x1e,saved);
 if(ret>=0)ret=restore;
unlock:
 phy_unlock_mdio_bus(phy);
 return ret;
}
static int cm5_ext_read(struct phy_device *phy, u16 reg)
{
 int saved, ret, restore;
 phy_lock_mdio_bus(phy);
 saved=__phy_read(phy,0x1e);
 if(saved<0){ret=saved;goto unlock;}
 ret=__phy_write(phy,0x1e,reg);
 if(ret>=0)ret=__phy_read(phy,0x1f);
 restore=__phy_write(phy,0x1e,saved);
 if(ret>=0 && restore<0)ret=restore;
unlock:
 phy_unlock_mdio_bus(phy);
 return ret;
}
static int cm5_probe(struct phy_device *phy)
{
 int mode=cm5_ext_read(phy,0xa001);
 if(mode<0)return mode;
 /* This board is strapped UTP->RGMII; fiber/SGMII is outside this module. */
 return (mode & 7) ? -ENODEV : 0;
}
static int cm5_delay_code(u32 ps, bool rx, u16 *coarse)
{
 *coarse=0;
 if(ps<=2250 && ps%150==0)return ps/150;
 if(rx && ps>=1900 && ps<=4150 && (ps-1900)%150==0){
  *coarse=BIT(8);return (ps-1900)/150;
 }
 return -EINVAL;
}
static int cm5_config(struct phy_device *phy)
{
 u32 rx_ps=1900,tx_ps=1950,actual_rx=0,actual_tx=0;
 u16 rx_coarse=0,unused=0,clock=0,delays=0;
 int rx,tx,ret;
 if(phy->mdio.dev.of_node){
  of_property_read_u32(phy->mdio.dev.of_node,"rx-internal-delay-ps",&rx_ps);
  of_property_read_u32(phy->mdio.dev.of_node,"tx-internal-delay-ps",&tx_ps);
 }
 rx=cm5_delay_code(rx_ps,true,&rx_coarse);
 tx=cm5_delay_code(tx_ps,false,&unused);
 if(rx<0 || tx<0)return -EINVAL;
 switch(phy->interface){
 case PHY_INTERFACE_MODE_RGMII:break;
 case PHY_INTERFACE_MODE_RGMII_RXID:clock=rx_coarse;delays=rx<<10;actual_rx=rx_ps;break;
 case PHY_INTERFACE_MODE_RGMII_TXID:delays=tx;actual_tx=tx_ps;break;
 case PHY_INTERFACE_MODE_RGMII_ID:clock=rx_coarse;delays=(rx<<10)|tx;actual_rx=rx_ps;actual_tx=tx_ps;break;
 default:return -EOPNOTSUPP;
 }
 ret=cm5_ext_change(phy,0xa001,BIT(8),clock);
 if(ret<0)return ret;
 ret=cm5_ext_change(phy,0xa003,GENMASK(13,10)|GENMASK(3,0),delays);
 if(ret<0)return ret;
 ret=cm5_ext_change(phy,0x27,BIT(15),0); /* Disable auto-sleep. */
 if(ret<0)return ret;
 ret=cm5_ext_change(phy,0x0c,BIT(12),0); /* Keep RX clock available. */
 if(ret>=0)phydev_info(phy,"CM5 YT8521 configured: %s RX=%ups TX=%ups\n",
      phy_modes(phy->interface),actual_rx,actual_tx);
 return ret;
}
static int cm5_resume(struct phy_device *phy)
{
 int ret=genphy_resume(phy);
 return ret<0 ? ret : cm5_config(phy);
}
static struct phy_driver cm5_driver[]={{
 PHY_ID_MATCH_EXACT(CM5_YT8521_ID),
 .name="YT8521 CM5 Ethernet",
 .features=PHY_GBIT_FEATURES,
 .probe=cm5_probe,
 .soft_reset=genphy_soft_reset,
 .config_init=cm5_config,
 .config_aneg=genphy_config_aneg,
 .read_status=genphy_read_status,
 .suspend=genphy_suspend,
 .resume=cm5_resume,
}};
module_phy_driver(cm5_driver);
static const struct mdio_device_id cm5_ids[]={
 {PHY_ID_MATCH_EXACT(CM5_YT8521_ID)},{}
};
MODULE_DEVICE_TABLE(mdio,cm5_ids);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("CM5 YT8521 UTP/RGMII PHY support with explicit clock delays");
