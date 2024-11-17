// SPDX-License-Identifier: GPL-2.0

#include <common.h>
#include <phy.h>
#include <linux/delay.h>

/* Extended Register's Address Offset Register */
#define YTPHY_PAGE_SELECT				0x1E
/* Extended Register's Data Register */
#define YTPHY_PAGE_DATA					0x1F

#define YT8521_EXTREG_SLEEP_CONTROL1	0x27
#define YT8521_EN_SLEEP_SW_BIT			15

#define YT8521SH_EXT_LED2_CFG			0xA00E
#define YT8521SH_EXT_LED1_CFG			0xA00D
#define YT8521SH_EXT_COMMON_RGMII_CFG1	0xA003
#define YT8521SH_EXT_REG_TX_OFFSET		0
#define YT8521SH_EXT_REG_RX_OFFSET		10

#define YT8521_REG_SPACE_SELECT_REG		0xA000
#define YT8521_RSSR_SPACE_MASK			BIT(1)
#define YT8521_RSSR_FIBER_SPACE			(0x1 << 1)
#define YT8521_RSSR_UTP_SPACE			(0x0 << 1)

#define YTPHY_SPECIFIC_STATUS_REG		0x11
#define YTPHY_SSR_SPEED_MASK			((0x3 << 14))
#define YTPHY_SSR_SPEED_10M				((0x0 << 14))
#define YTPHY_SSR_SPEED_100M			((0x1 << 14))
#define YTPHY_SSR_SPEED_1000M			((0x2 << 14))
#define YTPHY_SSR_DUPLEX				BIT(13)
#define YTPHY_SSR_LINK					BIT(10)

static int ytphy_read_ext(struct phy_device *phydev, u16 regnum)
{
	int ret;

	ret = phy_write(phydev, MDIO_DEVAD_NONE, YTPHY_PAGE_SELECT, regnum);
	if (ret < 0)
		return ret;
	
	return phy_read(phydev, MDIO_DEVAD_NONE, YTPHY_PAGE_DATA);
}

static int ytphy_write_ext(struct phy_device *phydev, u16 regnum, u16 val)
{
	int ret;

	ret = phy_write(phydev, MDIO_DEVAD_NONE, YTPHY_PAGE_SELECT, regnum);
	if (ret < 0)
		return ret;

	return phy_write(phydev, MDIO_DEVAD_NONE, YTPHY_PAGE_DATA, val);
}

static int yt8521sh_config(struct phy_device *phydev)
{
	ofnode node;
	int ret;
	int value;
	u16 phy_tx_delay = 0;
	u16 phy_rx_delay = 0;

	/* Get phy tx/rx delay vlaues from devicetree */
	node = phy_get_ofnode(phydev);
	if (ofnode_valid(node)) {
		ret = ofnode_read_u32(node, "phy-tx-delay", &value);
		if (ret == 0) {
			phy_tx_delay = value;
		}
		ret = ofnode_read_u32(node, "phy-rx-delay", &value);
		if (ret == 0) {
			phy_rx_delay = value;
		}
	}
	printf("%s: %s,%d,%d\n", __func__, 
		ofnode_get_name(node), 
		phy_tx_delay, phy_rx_delay);
	
	value = ytphy_read_ext(phydev, YT8521_REG_SPACE_SELECT_REG);
	if ((value & YT8521_RSSR_SPACE_MASK) == YT8521_RSSR_FIBER_SPACE) {
		ytphy_write_ext(phydev, YT8521_REG_SPACE_SELECT_REG, YT8521_RSSR_UTP_SPACE);
		printf("%s: YT8521_REG_SPACE_SELECT_REG\n", __func__);
	}
	
	/* disable auto sleep */
	value = ytphy_read_ext(phydev, YT8521_EXTREG_SLEEP_CONTROL1);
	value &= (~BIT(YT8521_EN_SLEEP_SW_BIT));
	ytphy_write_ext(phydev, YT8521_EXTREG_SLEEP_CONTROL1, value);
	
	/* enable RXC clock when no wire plug */
	value = ytphy_read_ext(phydev, 0xc);
	value &= ~(1 << 12);
	ytphy_write_ext(phydev, 0xc, value);
	
	/* Config LED2 to ON when phy link up */
	ytphy_write_ext(phydev, YT8521SH_EXT_LED2_CFG, 0x0070);

	/* Config LED1 to BLINK when phy link up and tx rx active */
	ytphy_write_ext(phydev, YT8521SH_EXT_LED1_CFG, 0x0670);

	/* Read rgmii cfg1 */
	value = ytphy_read_ext(phydev, YT8521SH_EXT_COMMON_RGMII_CFG1);
	value &= ~(0xf << YT8521SH_EXT_REG_TX_OFFSET);
	value |= ((phy_tx_delay & 0xf) << YT8521SH_EXT_REG_TX_OFFSET);
	value &= ~(0xf << YT8521SH_EXT_REG_RX_OFFSET);
	value |= ((phy_rx_delay & 0xf) << YT8521SH_EXT_REG_RX_OFFSET);
	/* Config phy tx/rx delay */
	ytphy_write_ext(phydev, YT8521SH_EXT_COMMON_RGMII_CFG1, value);

	genphy_config_aneg(phydev);

	return 0;
}

static int yt8521sh_read_status(struct phy_device *phydev)
{
	int value;
	
	value = phy_read(phydev, MDIO_DEVAD_NONE, YTPHY_SPECIFIC_STATUS_REG);
	if (value < 0) {
		printf("%s: YTPHY_SPECIFIC_STATUS_REG(%d)\n", __func__, value);
		return -1;
	}
	
	phydev->link = !!(value & YTPHY_SSR_LINK);
	if (phydev->link) {
		if (value & YTPHY_SSR_DUPLEX)
			phydev->duplex = DUPLEX_FULL;
		else
			phydev->duplex = DUPLEX_HALF;
		
		switch (value & YTPHY_SSR_SPEED_MASK) {
		case YTPHY_SSR_SPEED_10M:
			phydev->speed = SPEED_10;
			break;
		case YTPHY_SSR_SPEED_100M:
			phydev->speed = SPEED_100;
			break;
		case YTPHY_SSR_SPEED_1000M:
			phydev->speed = SPEED_1000;
			break;
		default:
			phydev->speed = -1;
			break;
		}
		printf("%s: link up, duplex=%d, speed=%d\n", __func__, 
			phydev->duplex, phydev->speed);
	} else {
		printf("%s: link down\n", __func__);
	}
	
	return 0;
}

static int yt8521sh_startup(struct phy_device *phydev)
{
	int ret;

	/* Wait for auto-negotiation to complete or fail */
	ret = genphy_update_link(phydev);
	if (ret)
		return ret;
	
	return yt8521sh_read_status(phydev);
}

static struct phy_driver YT8521SH_driver = {
	.name = "YT8521SH Gigabit Ethernet",
	.uid = 0x0000011a,
	.mask = 0x00000fff,
	.features = PHY_GBIT_FEATURES,
	.config = yt8521sh_config,
	.startup = yt8521sh_startup,
	.shutdown = genphy_shutdown,
};

int phy_yt8521sh_init(void)
{
	phy_register(&YT8521SH_driver);

	return 0;
}
