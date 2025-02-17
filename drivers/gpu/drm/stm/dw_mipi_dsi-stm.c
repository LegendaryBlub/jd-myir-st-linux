// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) STMicroelectronics SA 2017
 *
 * Authors: Philippe Cornu <philippe.cornu@st.com>
 *          Yannick Fertre <yannick.fertre@st.com>
 *          Raphaël Gallais-Pou <raphael.gallais-pou@foss.st.com>
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>

#include <video/mipi_display.h>

#include <drm/bridge/dw_mipi_dsi.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_print.h>

#define HWVER_130			0x31333000	/* IP version 1.30 */
#define HWVER_131			0x31333100	/* IP version 1.31 */
#define HWVER_141			0x31343100	/* IP version 1.41 */

/* DSI digital registers & bit definitions */
#define DSI_VERSION			0x00
#define VERSION				GENMASK(31, 8)

/* DSI wrapper registers & bit definitions */
/* Note: registers are named as in the Reference Manual */
#define DSI_WCFGR	0x0400		/* Wrapper ConFiGuration Reg */
#define WCFGR_DSIM	BIT(0)		/* DSI Mode */
#define WCFGR_COLMUX	GENMASK(3, 1)	/* COLor MUltipleXing */

#define DSI_WCR		0x0404		/* Wrapper Control Reg */
#define WCR_DSIEN	BIT(3)		/* DSI ENable */

#define DSI_WISR	0x040C		/* Wrapper Interrupt and Status Reg */
#define WISR_PLLLS	BIT(8)		/* PLL Lock Status */
#define WISR_RRS	BIT(12)		/* Regulator Ready Status */

#define DSI_WPCR0	0x0418		/* Wrapper Phy Conf Reg 0 */
#define WPCR0_UIX4	GENMASK(5, 0)	/* Unit Interval X 4 */
#define WPCR0_TDDL	BIT(16)		/* Turn Disable Data Lanes */

#define DSI_WRPCR	0x0430		/* Wrapper Regulator & Pll Ctrl Reg */
#define WRPCR_PLLEN	BIT(0)		/* PLL ENable */
#define WRPCR_NDIV	GENMASK(8, 2)	/* pll loop DIVision Factor */
#define WRPCR_IDF	GENMASK(14, 11)	/* pll Input Division Factor */
#define WRPCR_ODF	GENMASK(17, 16)	/* pll Output Division Factor */
#define WRPCR_REGEN	BIT(24)		/* REGulator ENable */
#define WRPCR_BGREN	BIT(28)		/* BandGap Reference ENable */
#define IDF_MIN		1
#define IDF_MAX		7
#define NDIV_MIN	10
#define NDIV_MAX	125
#define ODF_MIN		1
#define ODF_MAX		8

/* specific registers for hardware version 1.41 */
#define DSI_WPCR1	0x0430		/* Wrapper Phy Conf Reg 1 */
#define WPCR1_CCF	GENMASK(5, 0)	/* Configuration clock frequency */
#define WPCR1_HSFR	GENMASK(14, 8)	/* High speed frequency range in Tx mode */
#define WPCR1_DLD	BIT(16)		/* Data lanes 0 direction */

#define DSI_WRPCR0	0x0434		/* Wrapper regulator and PLL configuration register 0 */
#define WRPCR0_IDF	GENMASK(3, 0)	/* PLL input division ratio */
#define WRPCR0_NDIV	GENMASK(13, 4)	/* PLL InLoop division ratio */
#define WRPCR0_PSC	BIT(16)		/* PLL shadow control */

#define DSI_WRPCR1	0x0438		/* Wrapper regulator and PLL configuration register 1 */
#define WRPCR1_PROP	GENMASK(5, 0)	/* Proportional charge pump */
#define WRPCR1_GMP	GENMASK(7, 6)	/* Loop filter resistance */
#define WRPCR1_INT	GENMASK(13, 8)	/* Integral of charge pump */
#define WRPCR1_BIAS	GENMASK(22, 16)	/* Charge pump bias */
#define WRPCR1_VCO	GENMASK(27, 24)	/* VCO operating range */
#define WRPCR1_ODF	GENMASK(29, 28)	/* Output division factor */

#define DSI_WRPCR2	0x043C		/* Wrapper regulator and PLL configuration register 2 */
#define WRPCR2_SEL	GENMASK(1, 0)	/* Output selection for PLL Clock */
#define WRPCR2_PLLEN	BIT(8)		/* PLL ENable */
#define WRPCR2_UPD	BIT(16)		/* Update (copies) the PLL shadow registers */
#define WRPCR2_CLR	BIT(24)		/* Clears the PLL shadow registers to their reset values */
#define WRPCR2_FPLL	BIT(28)		/* Force PLL lock signal */

#define DSI_PTCR0	0x00B4		/* Host PHY test control register 0 */
#define PTCR0_TRSEN	BIT(0)		/* Test-interface reset enable for the TDI bus */
#define PTCR0_TCKEN	BIT(1)		/* Test-interface clock enable for the TDI bus */

#define DSI_PCTLR	0x00A0		/* Host PHY control register */
#define PCTLR_PWEN	BIT(0)		/* Power enable */
#define PCTLR_DEN	BIT(1)		/* Digital enable */
#define PCTLR_CKEN	BIT(2)		/* Clock enable */
#define PCTLR_UCKEN	BIT(3)		/* ULPS clock enable */

#define DSI_PSR		0x00B0		/* Host PHY status register */
#define PSR_PSSC	BIT(2)		/* PHY stop state clock lane */

#define IDF_PHY_141_MIN		1
#define IDF_PHY_141_MAX		16
#define NDIV_PHY_141_MIN	64
#define NDIV_PHY_141_MAX	625

/* dsi color format coding according to the datasheet */
enum dsi_color {
	DSI_RGB565_CONF1,
	DSI_RGB565_CONF2,
	DSI_RGB565_CONF3,
	DSI_RGB666_CONF1,
	DSI_RGB666_CONF2,
	DSI_RGB888,
};

#define LANE_MIN_KBPS	31250
#define LANE_MAX_KBPS	500000

#define LANE_MIN_PHY_141_KBPS	80000
#define LANE_MAX_PHY_141_KBPS	2500000
#define FVCO_MIN_PHY_141_KBPS	320000
#define FVCO_MAX_PHY_141_KBPS	1250000

/* Sleep & timeout for regulator on/off, pll lock/unlock & fifo empty */
#define SLEEP_US	1000
#define TIMEOUT_US	200000

struct dw_mipi_dsi_stm {
	void __iomem *base;
	struct device *dev;
	struct clk *pllref_clk;
	struct clk *pclk;
	struct clk *px_clk;
	struct clk_hw txbyte_clk;
	struct dw_mipi_dsi *dsi;
	u32 hw_version;
	int lane_min_kbps;
	int lane_max_kbps;
	struct regulator *vdd_supply;
	struct regulator *vdda18_supply;
	struct dw_mipi_dsi_plat_data pdata;
	unsigned int lane_mbps;
	u32 format;
	bool probe_done;
};

static inline void dsi_write(struct dw_mipi_dsi_stm *dsi, u32 reg, u32 val)
{
	writel(val, dsi->base + reg);
}

static inline u32 dsi_read(struct dw_mipi_dsi_stm *dsi, u32 reg)
{
	return readl(dsi->base + reg);
}

static inline void dsi_set(struct dw_mipi_dsi_stm *dsi, u32 reg, u32 mask)
{
	dsi_write(dsi, reg, dsi_read(dsi, reg) | mask);
}

static inline void dsi_clear(struct dw_mipi_dsi_stm *dsi, u32 reg, u32 mask)
{
	dsi_write(dsi, reg, dsi_read(dsi, reg) & ~mask);
}

static inline void dsi_update_bits(struct dw_mipi_dsi_stm *dsi, u32 reg,
				   u32 mask, u32 val)
{
	dsi_write(dsi, reg, (dsi_read(dsi, reg) & ~mask) | val);
}

static enum dsi_color dsi_color_from_mipi(enum mipi_dsi_pixel_format fmt)
{
	switch (fmt) {
	case MIPI_DSI_FMT_RGB888:
		return DSI_RGB888;
	case MIPI_DSI_FMT_RGB666:
		return DSI_RGB666_CONF2;
	case MIPI_DSI_FMT_RGB666_PACKED:
		return DSI_RGB666_CONF1;
	case MIPI_DSI_FMT_RGB565:
		return DSI_RGB565_CONF1;
	default:
		DRM_DEBUG_DRIVER("MIPI color invalid, so we use rgb888\n");
	}
	return DSI_RGB888;
}

static int dsi_pll_get_clkout_khz(int clkin_khz, int idf, int ndiv, int odf)
{
	int divisor = idf * odf;

	/* prevent from division by 0 */
	if (!divisor)
		return 0;

	return DIV_ROUND_CLOSEST(clkin_khz * ndiv, divisor);
}

static int dsi_pll_get_params(struct dw_mipi_dsi_stm *dsi,
			      int clkin_khz, int clkout_khz,
			      int *idf, int *ndiv, int *odf)
{
	int i, o, n, n_min, n_max;
	int fvco_min, fvco_max, delta, best_delta; /* all in khz */

	/* Early checks preventing division by 0 & odd results */
	if (clkin_khz <= 0 || clkout_khz <= 0)
		return -EINVAL;

	fvco_min = dsi->lane_min_kbps * 2 * ODF_MAX;
	fvco_max = dsi->lane_max_kbps * 2 * ODF_MIN;

	best_delta = 1000000; /* big started value (1000000khz) */

	for (i = IDF_MIN; i <= IDF_MAX; i++) {
		/* Compute ndiv range according to Fvco */
		n_min = ((fvco_min * i) / (2 * clkin_khz)) + 1;
		n_max = (fvco_max * i) / (2 * clkin_khz);

		/* No need to continue idf loop if we reach ndiv max */
		if (n_min >= NDIV_MAX)
			break;

		/* Clamp ndiv to valid values */
		if (n_min < NDIV_MIN)
			n_min = NDIV_MIN;
		if (n_max > NDIV_MAX)
			n_max = NDIV_MAX;

		for (o = ODF_MIN; o <= ODF_MAX; o *= 2) {
			n = DIV_ROUND_CLOSEST(i * o * clkout_khz, clkin_khz);
			/* Check ndiv according to vco range */
			if (n < n_min || n > n_max)
				continue;
			/* Check if new delta is better & saves parameters */
			delta = dsi_pll_get_clkout_khz(clkin_khz, i, n, o) -
				clkout_khz;
			if (delta < 0)
				delta = -delta;
			if (delta < best_delta) {
				*idf = i;
				*ndiv = n;
				*odf = o;
				best_delta = delta;
			}
			/* fast return in case of "perfect result" */
			if (!delta)
				return 0;
		}
	}

	return 0;
}

struct dphy_pll_parameter_map {
	u32 data_rate;	/* upper margin of frequency range */
	u8 hs_freq;	/* hsfreqrange */
	u8 odf;
	u8 vco;
	u8 prop;
};

static const struct dphy_pll_parameter_map dppa_map[] = {
	{80, 0x00, 0x03, 0x0F, 0x0B},
	{90, 0x10, 0x03, 0x0F, 0x0B},
	{100, 0x20, 0x03, 0x0F, 0x0B},
	{110, 0x30, 0x03, 0x09, 0x0B},
	{120, 0x01, 0x03, 0x09, 0x0B},
	{130, 0x11, 0x03, 0x09, 0x0B},
	{140, 0x21, 0x03, 0x09, 0x0B},
	{150, 0x31, 0x03, 0x09, 0x0B},
	{160, 0x02, 0x02, 0x0F, 0x0B},
	{170, 0x12, 0x02, 0x0F, 0x0B},
	{180, 0x22, 0x02, 0x0F, 0x0B},
	{190, 0x32, 0x02, 0x0F, 0x0B},
	{205, 0x03, 0x02, 0x0F, 0x0B},
	{220, 0x13, 0x02, 0x09, 0x0B},
	{235, 0x23, 0x02, 0x09, 0x0B},
	{250, 0x33, 0x02, 0x09, 0x0B},
	{275, 0x04, 0x02, 0x09, 0x0B},
	{300, 0x14, 0x02, 0x09, 0x0B},
	{325, 0x25, 0x01, 0x0F, 0x0B},
	{350, 0x35, 0x01, 0x0F, 0x0B},
	{400, 0x05, 0x01, 0x0F, 0x0B},
	{450, 0x16, 0x01, 0x09, 0x0B},
	{500, 0x26, 0x01, 0x09, 0x0B},
	{550, 0x37, 0x01, 0x09, 0x0B},
	{600, 0x07, 0x01, 0x09, 0x0B},
	{650, 0x18, 0x00, 0x0F, 0x0B},
	{700, 0x28, 0x00, 0x0F, 0x0B},
	{750, 0x39, 0x00, 0x0F, 0x0B},
	{800, 0x09, 0x00, 0x0F, 0x0B},
	{850, 0x19, 0x00, 0x09, 0x0B},
	{900, 0x29, 0x00, 0x09, 0x0B},
	{950, 0x3A, 0x00, 0x09, 0x0B},
	{1000, 0x0A, 0x00, 0x09, 0x0B},
	{1050, 0x1A, 0x00, 0x09, 0x0B},
	{1100, 0x2A, 0x00, 0x09, 0x0B},
	{1150, 0x3B, 0x00, 0x09, 0x0B},
	{1200, 0x0B, 0x00, 0x09, 0x0B},
	{1250, 0x1B, 0x00, 0x09, 0x0B},
	{1300, 0x2B, 0x00, 0x03, 0x0B},
	{1350, 0x3C, 0x00, 0x03, 0x0B},
	{1400, 0x0C, 0x00, 0x03, 0x0B},
	{1450, 0x1C, 0x00, 0x03, 0x0B},
	{1500, 0x2C, 0x00, 0x03, 0x0B},
	{1550, 0x3D, 0x00, 0x03, 0x0B},
	{1600, 0x0D, 0x00, 0x03, 0x0B},
	{1650, 0x1D, 0x00, 0x03, 0x0B},
	{1700, 0x2E, 0x00, 0x03, 0x0B},
	{1750, 0x3E, 0x00, 0x03, 0x0B},
	{1800, 0x0E, 0x00, 0x03, 0x0B},
	{1850, 0x1E, 0x00, 0x03, 0x0B},
	{1900, 0x2F, 0x00, 0x03, 0x0B},
	{1950, 0x3F, 0x00, 0x03, 0x0B},
	{2000, 0x0F, 0x00, 0x03, 0x0B},
	{2050, 0x40, 0x00, 0x03, 0x0B},
	{2100, 0x41, 0x00, 0x03, 0x0B},
	{2150, 0x42, 0x00, 0x03, 0x0B},
	{2200, 0x43, 0x00, 0x01, 0x0B},
	{2250, 0x44, 0x00, 0x01, 0x0B},
	{2300, 0x45, 0x00, 0x01, 0x0C},
	{2350, 0x46, 0x00, 0x01, 0x0C},
	{2400, 0x47, 0x00, 0x01, 0x0C},
	{2450, 0x48, 0x00, 0x01, 0x0C},
	{2500, 0x49, 0x00, 0x01, 0x0C}
};

static int dsi_phy_141_pll_get_params(struct dw_mipi_dsi_stm *dsi,
				      int clkin_khz, int clkout_khz,
				      int *idf, int *ndiv, int *odf, int *index)
{
	int i, n;
	int delta, best_delta; /* all in khz */

	/* Early checks preventing division by 0 & odd results */
	if (clkin_khz <= 0 || clkout_khz <= 0)
		return -EINVAL;

	/* find frequency mapping */
	for (i = 0; i < ARRAY_SIZE(dppa_map); i++) {
		if (dsi->lane_mbps < dppa_map[i].data_rate) {
			if (i == ARRAY_SIZE(dppa_map) - 1)
				DRM_WARN("Could not find frequency mapped index\n");
			i--;
			break;
		}
	}

	/* Save index only if reference exists */
	if (index)
		*index = i;

	*odf = int_pow(2, ((dppa_map[i].odf & WRPCR1_ODF) >> 28));

	best_delta = 1000000; /* big started value (1000000khz) */

	for (i = IDF_PHY_141_MIN; i <= IDF_PHY_141_MAX; i++) {
		for (n = NDIV_PHY_141_MIN; n <= NDIV_PHY_141_MAX; n++) {
			/* Check if new delta is better & saves parameters */
			delta = dsi_pll_get_clkout_khz(clkin_khz, i, n, *odf) - clkout_khz;

			if (delta < 0)
				delta = -delta;
			if (delta < best_delta) {
				*idf = i;
				*ndiv = n;
				best_delta = delta;
			}
			/* fast return in case of "perfect result" */
			if (!delta)
				return 0;
		}
	}

	return 0;
}

static int dw_mipi_dsi_phy_141_init(void *priv_data)
{
	struct dw_mipi_dsi_stm *dsi = priv_data;
	int ret;

	DRM_DEBUG_DRIVER("\n");

	/* Select video mode by resetting DSIM bit */
	dsi_clear(dsi, DSI_WCFGR, WCFGR_DSIM);

	/* Select the color coding */
	dsi_update_bits(dsi, DSI_WCFGR, WCFGR_COLMUX,
			dsi_color_from_mipi(dsi->format) << 1);

	dsi_write(dsi, DSI_PCTLR, 0x00);

	/* clear the pll shadow regs */
	dsi_set(dsi, DSI_WRPCR2, WRPCR2_CLR);
	mdelay(1);

	dsi_clear(dsi, DSI_WRPCR2, WRPCR2_CLR);
	mdelay(1);

	/* set testclr = 1 */
	dsi_set(dsi, DSI_PTCR0, PTCR0_TRSEN);
	mdelay(1);

	dsi_clear(dsi, DSI_PTCR0, PTCR0_TRSEN);
	mdelay(1);

	if (clk_hw_is_enabled(&dsi->txbyte_clk))
		clk_disable_unprepare(dsi->txbyte_clk.clk);

	/* dummy set rate ... */
	clk_set_rate(dsi->txbyte_clk.clk, dsi->lane_mbps * 1000000);

	ret = clk_set_rate(dsi->txbyte_clk.clk, dsi->lane_mbps * 1000000 / 2);
	if (ret)
		return ret;

	ret = clk_prepare_enable(dsi->txbyte_clk.clk);

	return ret;
}

static int
dw_mipi_dsi_phy_141_get_lane_mbps(void *priv_data,
				  const struct drm_display_mode *mode,
				  unsigned long mode_flags, u32 lanes,
				  u32 format, unsigned int *lane_mbps)
{
	struct dw_mipi_dsi_stm *dsi = priv_data;
	unsigned int pll_out_khz, pll_in_khz;
	u32 ndiv, odf, idf;
	int bpp;

	/* Compute requested pll out */
	dsi->format = format;
	bpp = mipi_dsi_pixel_format_to_bpp(dsi->format);
	pll_out_khz = mode->clock * bpp / (lanes * 2);

	/* Add 20% to pll out to be higher than pixel bw (burst mode only) */
	if (mode_flags & MIPI_DSI_MODE_VIDEO_BURST)
		pll_out_khz = (pll_out_khz * 12) / 10;

	if (pll_out_khz > dsi->lane_max_kbps) {
		pll_out_khz = dsi->lane_max_kbps;
		DRM_WARN("Warning max phy mbps is used\n");
	}
	if (pll_out_khz < dsi->lane_min_kbps) {
		pll_out_khz = dsi->lane_min_kbps;
		DRM_WARN("Warning min phy mbps is used\n");
	}

	pll_in_khz = (unsigned int)(clk_get_rate(dsi->pllref_clk) / 1000);

	dsi_phy_141_pll_get_params(dsi, pll_in_khz, pll_out_khz, &idf, &ndiv, &odf, NULL);

	/* Get the adjusted lane data rate value, lane data rate = 2 * pll output */
	*lane_mbps = 2 * dsi_pll_get_clkout_khz(pll_in_khz, idf, ndiv, odf) / 1000;
	dsi->lane_mbps = *lane_mbps;

	DRM_DEBUG_DRIVER("lane_mbps %d\n", *lane_mbps);

	return 0;
}

#define clk_to_dw_mipi_dsi_stm(clk) \
	container_of(clk, struct dw_mipi_dsi_stm, txbyte_clk)

static void dw_mipi_dsi_clk_disable(struct clk_hw *clk)
{
	struct dw_mipi_dsi_stm *dsi = clk_to_dw_mipi_dsi_stm(clk);
	int ret;

	DRM_DEBUG_DRIVER("\n");

	if (!dsi->probe_done)
		return;

	ret = clk_prepare_enable(dsi->pclk);
	if (ret) {
		DRM_ERROR("%s: Failed to enable peripheral clk\n", __func__);
		return;
	}

	if (dsi->hw_version == HWVER_141) {
		/* Disable the DSI PLL */
		dsi_clear(dsi, DSI_WRPCR2, WRPCR2_PLLEN);
	} else {
		/* Disable the DSI PLL */
		dsi_clear(dsi, DSI_WRPCR, WRPCR_PLLEN);

		/* Disable the regulator */
		dsi_clear(dsi, DSI_WRPCR, WRPCR_REGEN | WRPCR_BGREN);
	}

	clk_disable_unprepare(dsi->pclk);
}

static int dw_mipi_dsi_clk_enable(struct clk_hw *clk)
{
	struct dw_mipi_dsi_stm *dsi = clk_to_dw_mipi_dsi_stm(clk);
	u32 val;
	int ret;

	DRM_DEBUG_DRIVER("\n");

	if (!dsi->probe_done)
		return 0;

	ret = clk_prepare_enable(dsi->pclk);
	if (ret) {
		DRM_ERROR("%s: Failed to enable peripheral clk\n", __func__);
		return ret;
	}

	if (dsi->hw_version == HWVER_141) {
		ret = readl_poll_timeout_atomic(dsi->base + DSI_PSR, val, val & PSR_PSSC,
						SLEEP_US, TIMEOUT_US);
		if (ret)
			DRM_ERROR("!TIMEOUT! waiting PLL, let's continue\n");

		dsi_set(dsi, DSI_WRPCR2, WRPCR2_PLLEN);
	} else {
		/* Enable the regulator */
		dsi_set(dsi, DSI_WRPCR, WRPCR_REGEN | WRPCR_BGREN);
		ret = readl_poll_timeout_atomic(dsi->base + DSI_WISR, val, val & WISR_RRS,
						SLEEP_US, TIMEOUT_US);
		if (ret)
			DRM_DEBUG_DRIVER("!TIMEOUT! waiting REGU, let's continue\n");

		/* Enable the DSI PLL & wait for its lock */
		dsi_set(dsi, DSI_WRPCR, WRPCR_PLLEN);
		ret = readl_poll_timeout_atomic(dsi->base + DSI_WISR, val, val & WISR_PLLLS,
						SLEEP_US, TIMEOUT_US);
		if (ret)
			DRM_DEBUG_DRIVER("!TIMEOUT! waiting PLL, let's continue\n");
	}

	clk_disable_unprepare(dsi->pclk);

	return 0;
}

static int dw_mipi_dsi_clk_is_enabled(struct clk_hw *hw)
{
	struct dw_mipi_dsi_stm *dsi = clk_to_dw_mipi_dsi_stm(hw);
	int ret;

	ret = clk_prepare_enable(dsi->pclk);
	if (ret) {
		DRM_ERROR("%s: Failed to enable peripheral clk\n", __func__);
		return false;
	}

	if (dsi->hw_version == HWVER_141)
		ret = dsi_read(dsi, DSI_WRPCR2) & WRPCR2_PLLEN;
	else
		ret = dsi_read(dsi, DSI_WRPCR) & WRPCR_PLLEN;

	clk_disable_unprepare(dsi->pclk);

	return ret;
}

static unsigned long dw_mipi_dsi_clk_recalc_rate(struct clk_hw *hw,
						 unsigned long parent_rate)
{
	struct dw_mipi_dsi_stm *dsi = clk_to_dw_mipi_dsi_stm(hw);
	unsigned int idf, ndiv, odf, pll_in_khz, pll_out_khz;
	int ret;
	u32 val;

	DRM_DEBUG_DRIVER("\n");

	ret = clk_prepare_enable(dsi->pclk);
	if (ret) {
		DRM_ERROR("%s: Failed to enable peripheral clk\n", __func__);
		return -EINVAL;
	}

	pll_in_khz = (unsigned int)(parent_rate / 1000);

	if (dsi->hw_version == HWVER_141) {
		val = dsi_read(dsi, DSI_WRPCR0);

		idf = (val & WRPCR0_IDF) + 1;
		ndiv = ((val & WRPCR0_NDIV) >> 4) + 2;

		val = dsi_read(dsi, DSI_WRPCR1);

		odf = int_pow(2, (val & WRPCR1_ODF) >> 28);
	} else {
		val = dsi_read(dsi, DSI_WRPCR);

		idf = (val & WRPCR_IDF) >> 11;
		if (!idf)
			idf = 1;
		ndiv = (val & WRPCR_NDIV) >> 2;
		odf = int_pow(2, (val & WRPCR_ODF) >> 16);
	}

	/* Get the adjusted pll out value */
	pll_out_khz = dsi_pll_get_clkout_khz(pll_in_khz, idf, ndiv, odf);

	clk_disable_unprepare(dsi->pclk);

	return (unsigned long)pll_out_khz * 1000;
}

static long dw_mipi_dsi_clk_round_rate(struct clk_hw *hw, unsigned long rate,
				       unsigned long *parent_rate)
{
	struct dw_mipi_dsi_stm *dsi = clk_to_dw_mipi_dsi_stm(hw);
	unsigned int idf, ndiv, odf, pll_in_khz, pll_out_khz;
	int ret;

	DRM_DEBUG_DRIVER("\n");

	ret = clk_prepare_enable(dsi->pclk);
	if (ret) {
		DRM_ERROR("%s: Failed to enable peripheral clk\n", __func__);
		return -EINVAL;
	}

	pll_in_khz = (unsigned int)(*parent_rate / 1000);

	/* Compute best pll parameters */
	idf = 0;
	ndiv = 0;
	odf = 0;

	if (dsi->hw_version == HWVER_141)
		ret = dsi_phy_141_pll_get_params(dsi, pll_in_khz, rate / 1000,
						 &idf, &ndiv, &odf, NULL);
	else
		ret = dsi_pll_get_params(dsi, pll_in_khz, rate / 1000,
					 &idf, &ndiv, &odf);
	if (ret)
		DRM_WARN("Warning dsi_pll_get_params(): bad params\n");

	/* Get the adjusted pll out value */
	pll_out_khz = dsi_pll_get_clkout_khz(pll_in_khz, idf, ndiv, odf);

	clk_disable_unprepare(dsi->pclk);

	return pll_out_khz * 1000;
}

static int dw_mipi_dsi_clk_set_rate(struct clk_hw *hw, unsigned long rate,
				    unsigned long parent_rate)
{
	struct dw_mipi_dsi_stm *dsi = clk_to_dw_mipi_dsi_stm(hw);
	u32 val, ccf, prop, gmp, int1, bias, vco;
	unsigned int idf, ndiv, odf, pll_in_khz, pll_out_khz, hsfreq;
	int ret, dppa_index;

	DRM_DEBUG_DRIVER("\n");

	pll_in_khz = (unsigned int)(parent_rate / 1000);

	ret = clk_prepare_enable(dsi->pclk);
	if (ret) {
		DRM_ERROR("%s: Failed to enable peripheral clk\n", __func__);
		return -EINVAL;
	}

	/* Compute best pll parameters */
	idf = 0;
	ndiv = 0;
	odf = 0;

	if (dsi->hw_version == HWVER_141) {
		/* Compute requested pll out, pll out is the half of the lane data rate */
		pll_out_khz = dsi->lane_mbps * 1000 / 2;

		ret = dsi_phy_141_pll_get_params(dsi, pll_in_khz, pll_out_khz,
						 &idf, &ndiv, &odf, &dppa_index);
		if (ret)
			DRM_WARN("Warning dsi_pll_get_params(): bad params\n");

		ccf = ((pll_in_khz / 1000 - 17)) * 4;
		hsfreq = dppa_map[dppa_index].hs_freq;

		vco = dppa_map[dppa_index].vco;
		bias = 0x10;
		int1 = 0x00;
		gmp = 0x01;
		prop = dppa_map[dppa_index].prop;

		/* set DLD, HSFR & CCF */
		val = (hsfreq << 8) | ccf;
		dsi_write(dsi, DSI_WPCR1, val);

		val = ((ndiv - 2) << 4) | (idf - 1);
		dsi_write(dsi, DSI_WRPCR0, val);

		val = ((odf - 1) << 28) | (vco << 24) | (bias << 16)
		       | (int1 << 8) | (gmp << 6) | prop;
		dsi_write(dsi, DSI_WRPCR1, val);

		dsi_write(dsi, DSI_PCTLR, PCTLR_CKEN);

		dsi_update_bits(dsi, DSI_WRPCR2, WRPCR2_SEL, 0x01);

		dsi_set(dsi, DSI_WRPCR2, WRPCR2_UPD);
		mdelay(1);

		dsi_clear(dsi, DSI_WRPCR2, WRPCR2_UPD);
		mdelay(1);

		dsi_set(dsi, DSI_PCTLR, PCTLR_PWEN | PCTLR_DEN);
	} else {
		ret = dsi_pll_get_params(dsi, pll_in_khz, rate / 1000,
					 &idf, &ndiv, &odf);
		if (ret)
			DRM_WARN("Warning dsi_pll_get_params(): bad params\n");

		/* Get the adjusted pll out value */
		pll_out_khz = dsi_pll_get_clkout_khz(pll_in_khz, idf, ndiv, odf);

		/* Set the PLL division factors */
		dsi_update_bits(dsi, DSI_WRPCR, WRPCR_NDIV | WRPCR_IDF | WRPCR_ODF,
				(ndiv << 2) | (idf << 11) | ((ffs(odf) - 1) << 16));

		/* Compute uix4 & set the bit period in high-speed mode */
		val = 4000000 / pll_out_khz;
		dsi_update_bits(dsi, DSI_WPCR0, WPCR0_UIX4, val);
	}

	clk_disable_unprepare(dsi->pclk);

	return 0;
}

static void dw_mipi_dsi_clk_unregister(void *data)
{
	struct dw_mipi_dsi_stm *dsi = data;

	DRM_DEBUG_DRIVER("\n");

	of_clk_del_provider(dsi->dev->of_node);
	clk_hw_unregister(&dsi->txbyte_clk);
}

static const struct clk_ops dw_mipi_dsi_stm_clk_ops = {
	.enable = dw_mipi_dsi_clk_enable,
	.disable = dw_mipi_dsi_clk_disable,
	.is_enabled = dw_mipi_dsi_clk_is_enabled,
	.recalc_rate = dw_mipi_dsi_clk_recalc_rate,
	.round_rate = dw_mipi_dsi_clk_round_rate,
	.set_rate = dw_mipi_dsi_clk_set_rate,
};

static struct clk_init_data cdata_init = {
	.name = "ck_dsi_phy",
	.ops = &dw_mipi_dsi_stm_clk_ops,
	.parent_names = (const char * []) {"ck_hse"},
	.num_parents = 1,
};

static struct clk_init_data cdata_init_141 = {
	.name = "txbyteclk",
	.ops = &dw_mipi_dsi_stm_clk_ops,
	.parent_names = (const char * []) {"clk_phy_dsi"},
	.num_parents = 1,
};

static int dw_mipi_dsi_clk_register(struct dw_mipi_dsi_stm *dsi,
				    struct device *dev)
{
	struct device_node *node = dev->of_node;
	int ret;

	DRM_DEBUG_DRIVER("Registering clk\n");

	if (dsi->hw_version == HWVER_131 || dsi->hw_version == HWVER_130) {
		dsi->txbyte_clk.init = &cdata_init;
	} else if (dsi->hw_version == HWVER_141) {
		dsi->txbyte_clk.init = &cdata_init_141;
	} else {
		DRM_ERROR("Version hw_clk not supported\n");
		return -ENODEV;
	}

	ret = clk_hw_register(dev, &dsi->txbyte_clk);
	if (ret)
		return ret;

	ret = of_clk_add_hw_provider(node, of_clk_hw_simple_get,
				     &dsi->txbyte_clk);
	if (ret)
		clk_hw_unregister(&dsi->txbyte_clk);

	return ret;
}

static int dw_mipi_dsi_phy_init(void *priv_data)
{
	struct dw_mipi_dsi_stm *dsi = priv_data;
	int ret = 0;

	if (!clk_hw_is_enabled(&dsi->txbyte_clk))
		ret = clk_prepare_enable(dsi->txbyte_clk.clk);

	return ret;
}

static void dw_mipi_dsi_phy_power_on(void *priv_data)
{
	struct dw_mipi_dsi_stm *dsi = priv_data;

	DRM_DEBUG_DRIVER("\n");

	/* Enable the DSI wrapper */
	dsi_set(dsi, DSI_WCR, WCR_DSIEN);
}

static void dw_mipi_dsi_phy_power_off(void *priv_data)
{
	struct dw_mipi_dsi_stm *dsi = priv_data;

	DRM_DEBUG_DRIVER("\n");

	clk_disable_unprepare(dsi->txbyte_clk.clk);

	/* Disable the DSI wrapper */
	dsi_clear(dsi, DSI_WCR, WCR_DSIEN);
}

static int
dw_mipi_dsi_get_lane_mbps(void *priv_data, const struct drm_display_mode *mode,
			  unsigned long mode_flags, u32 lanes, u32 format,
			  unsigned int *lane_mbps)
{
	struct dw_mipi_dsi_stm *dsi = priv_data;
	unsigned int pll_in_khz, pll_out_khz;
	int ret, bpp;

	pll_in_khz = (unsigned int)(clk_get_rate(dsi->pllref_clk) / 1000);

	/* Compute requested pll out */
	bpp = mipi_dsi_pixel_format_to_bpp(format);
	pll_out_khz = mode->clock * bpp / lanes;

	/* Add 20% to pll out to be higher than pixel bw (burst mode only) */
	if (mode_flags & MIPI_DSI_MODE_VIDEO_BURST)
		pll_out_khz = (pll_out_khz * 12) / 10;

	if (pll_out_khz > dsi->lane_max_kbps) {
		pll_out_khz = dsi->lane_max_kbps;
		DRM_WARN("Warning max phy mbps is used\n");
	}
	if (pll_out_khz < dsi->lane_min_kbps) {
		pll_out_khz = dsi->lane_min_kbps;
		DRM_WARN("Warning min phy mbps is used\n");
	}

	ret = clk_set_rate((dsi->txbyte_clk.clk), pll_out_khz * 1000);
	if (ret)
		DRM_DEBUG_DRIVER("ERROR Could not set rate of %d to %s clk->name",
				 pll_out_khz, clk_hw_get_name(&dsi->txbyte_clk));

	/* Select video mode by resetting DSIM bit */
	dsi_clear(dsi, DSI_WCFGR, WCFGR_DSIM);

	/* Select the color coding */
	dsi_update_bits(dsi, DSI_WCFGR, WCFGR_COLMUX,
			dsi_color_from_mipi(format) << 1);

	*lane_mbps = pll_out_khz / 1000;

	DRM_DEBUG_DRIVER("pll_in %ukHz pll_out %ukHz lane_mbps %uMHz\n",
			 pll_in_khz, pll_out_khz, *lane_mbps);

	return 0;
}

#define DSI_PHY_DELAY(fp, vp, mbps) DIV_ROUND_UP((fp) * (mbps) + 1000 * (vp), 8000)

static int
dw_mipi_dsi_phy_get_timing(void *priv_data, unsigned int lane_mbps,
			   struct dw_mipi_dsi_dphy_timing *timing)
{
	/*
	 * From STM32MP157 datasheet, valid for STM32F469, STM32F7x9, STM32H747
	 * phy_clkhs2lp_time = (272+136*UI)/(8*UI)
	 * phy_clklp2hs_time = (512+40*UI)/(8*UI)
	 * phy_hs2lp_time = (192+64*UI)/(8*UI)
	 * phy_lp2hs_time = (256+32*UI)/(8*UI)
	 */
	timing->clk_hs2lp = DSI_PHY_DELAY(272, 136, lane_mbps);
	timing->clk_lp2hs = DSI_PHY_DELAY(512, 40, lane_mbps);
	timing->data_hs2lp = DSI_PHY_DELAY(192, 64, lane_mbps);
	timing->data_lp2hs = DSI_PHY_DELAY(256, 32, lane_mbps);

	return 0;
}

struct hstt {
	unsigned int maxfreq;
	struct dw_mipi_dsi_dphy_timing timing;
};

#define HSTT(_maxfreq, _c_lp2hs, _c_hs2lp, _d_lp2hs, _d_hs2lp)	\
{					\
	.maxfreq = _maxfreq,		\
	.timing = {			\
		.clk_lp2hs = _c_lp2hs,	\
		.clk_hs2lp = _c_hs2lp,	\
		.data_lp2hs = _d_lp2hs,	\
		.data_hs2lp = _d_hs2lp,	\
	}				\
}

/* Table High-Speed Transition Times */
struct hstt hstt_phy_141_table[] = {
	HSTT(80, 21, 17, 15, 10),
	HSTT(90, 23, 17, 16, 10),
	HSTT(100, 22, 17, 16, 10),
	HSTT(110, 25, 18, 17, 11),
	HSTT(120, 26, 20, 18, 11),
	HSTT(130, 27, 19, 19, 11),
	HSTT(140, 27, 19, 19, 11),
	HSTT(150, 28, 20, 20, 12),
	HSTT(160, 30, 21, 22, 13),
	HSTT(170, 30, 21, 23, 13),
	HSTT(180, 31, 21, 23, 13),
	HSTT(190, 32, 22, 24, 13),
	HSTT(205, 35, 22, 25, 13),
	HSTT(220, 37, 26, 27, 15),
	HSTT(235, 38, 28, 27, 16),
	HSTT(250, 41, 29, 30, 17),
	HSTT(275, 43, 29, 32, 18),
	HSTT(300, 45, 32, 35, 19),
	HSTT(325, 48, 33, 36, 18),
	HSTT(350, 51, 35, 40, 20),
	HSTT(400, 59, 37, 44, 21),
	HSTT(450, 65, 40, 49, 23),
	HSTT(500, 71, 41, 54, 24),
	HSTT(550, 77, 44, 57, 26),
	HSTT(600, 82, 46, 64, 27),
	HSTT(650, 87, 48, 67, 28),
	HSTT(700, 94, 52, 71, 29),
	HSTT(750, 99, 52, 75, 31),
	HSTT(800, 105, 55, 82, 32),
	HSTT(850, 110, 58, 85, 32),
	HSTT(900, 115, 58, 88, 35),
	HSTT(950, 120, 62, 93, 36),
	HSTT(1000, 128, 63, 99, 38),
	HSTT(1050, 132, 65, 102, 38),
	HSTT(1100, 138, 67, 106, 39),
	HSTT(1150, 146, 69, 112, 42),
	HSTT(1200, 151, 71, 117, 43),
	HSTT(1250, 153, 74, 120, 45),
	HSTT(1300, 160, 73, 124, 46),
	HSTT(1350, 165, 76, 130, 47),
	HSTT(1400, 172, 78, 134, 49),
	HSTT(1450, 177, 80, 138, 49),
	HSTT(1500, 183, 81, 143, 52),
	HSTT(1550, 191, 84, 147, 52),
	HSTT(1600, 194, 85, 152, 52),
	HSTT(1650, 201, 86, 155, 53),
	HSTT(1700, 208, 88, 161, 53),
	HSTT(1750, 212, 89, 165, 53),
	HSTT(1800, 220, 90, 171, 54),
	HSTT(1850, 223, 92, 175, 55),
	HSTT(1900, 231, 91, 180, 56),
	HSTT(1950, 236, 95, 185, 56),
	HSTT(2000, 243, 97, 190, 58),
	HSTT(2050, 248, 99, 194, 59),
	HSTT(2100, 252, 100, 199, 61),
	HSTT(2150, 259, 102, 204, 62),
	HSTT(2200, 266, 105, 210, 63),
	HSTT(2250, 269, 109, 213, 65),
	HSTT(2300, 272, 109, 217, 66),
	HSTT(2350, 281, 112, 225, 66),
	HSTT(2400, 283, 115, 226, 67),
	HSTT(2450, 282, 115, 226, 67),
	HSTT(2500, 281, 118, 227, 68)
};

static int
dw_mipi_dsi_phy_141_get_timing(void *priv_data, unsigned int lane_mbps,
			       struct dw_mipi_dsi_dphy_timing *timing)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(hstt_phy_141_table); i++)
		if (lane_mbps < hstt_phy_141_table[i].maxfreq)
			break;

	if (i == ARRAY_SIZE(hstt_phy_141_table))
		i--;

	*timing = hstt_phy_141_table[i].timing;

	return 0;
}

#define CLK_TOLERANCE_HZ 50

static enum drm_mode_status
dw_mipi_dsi_stm_mode_valid(void *priv_data,
			   const struct drm_display_mode *mode,
			   unsigned long mode_flags, u32 lanes, u32 format)
{
	struct dw_mipi_dsi_stm *dsi = priv_data;
	unsigned int idf, ndiv, odf, pll_in_khz, pll_out_khz;
	int ret, bpp;

	bpp = mipi_dsi_pixel_format_to_bpp(format);
	if (bpp < 0)
		return MODE_BAD;

	/* Compute requested pll out */
	pll_out_khz = mode->clock * bpp / lanes;

	if (pll_out_khz > dsi->lane_max_kbps)
		return MODE_CLOCK_HIGH;

	if (mode_flags & MIPI_DSI_MODE_VIDEO_BURST) {
		/* Add 20% to pll out to be higher than pixel bw */
		pll_out_khz = (pll_out_khz * 12) / 10;
	} else {
		if (pll_out_khz < dsi->lane_min_kbps)
			return MODE_CLOCK_LOW;
	}

	/* Compute best pll parameters */
	idf = 0;
	ndiv = 0;
	odf = 0;
	pll_in_khz = clk_get_rate(dsi->pllref_clk) / 1000;
	ret = dsi_pll_get_params(dsi, pll_in_khz, pll_out_khz, &idf, &ndiv, &odf);
	if (ret) {
		DRM_WARN("Warning dsi_pll_get_params(): bad params\n");
		return MODE_ERROR;
	}

	if (!(mode_flags & MIPI_DSI_MODE_VIDEO_BURST)) {
		unsigned int px_clock_hz, target_px_clock_hz, lane_mbps;
		int dsi_short_packet_size_px, hfp, hsync, hbp, delay_to_lp;
		struct dw_mipi_dsi_dphy_timing dphy_timing;

		/* Get the adjusted pll out value */
		pll_out_khz = dsi_pll_get_clkout_khz(pll_in_khz, idf, ndiv, odf);

		px_clock_hz = DIV_ROUND_CLOSEST_ULL(1000ULL * pll_out_khz * lanes, bpp);
		target_px_clock_hz = mode->clock * 1000;
		/*
		 * Filter modes according to the clock value, particularly useful for
		 * hdmi modes that require precise pixel clocks.
		 */
		if (px_clock_hz < target_px_clock_hz - CLK_TOLERANCE_HZ ||
		    px_clock_hz > target_px_clock_hz + CLK_TOLERANCE_HZ)
			return MODE_CLOCK_RANGE;

		/* sync packets are codes as DSI short packets (4 bytes) */
		dsi_short_packet_size_px = DIV_ROUND_UP(4 * BITS_PER_BYTE, bpp);

		hfp = mode->hsync_start - mode->hdisplay;
		hsync = mode->hsync_end - mode->hsync_start;
		hbp = mode->htotal - mode->hsync_end;

		/* hsync must be longer than 4 bytes HSS packets */
		if (hsync < dsi_short_packet_size_px)
			return MODE_HSYNC_NARROW;

		if (mode_flags & MIPI_DSI_MODE_VIDEO_SYNC_PULSE) {
			/* HBP must be longer than 4 bytes HSE packets */
			if (hbp < dsi_short_packet_size_px)
				return MODE_HSYNC_NARROW;
			hbp -= dsi_short_packet_size_px;
		} else {
			/* With sync events HBP extends in the hsync */
			hbp += hsync - dsi_short_packet_size_px;
		}

		lane_mbps = pll_out_khz / 1000;

		if (dsi->hw_version < HWVER_141) {
			ret = dw_mipi_dsi_phy_get_timing(priv_data, lane_mbps, &dphy_timing);
			if (ret)
				return MODE_ERROR;
			/*
			 * In non-burst mode DSI has to enter in LP during HFP
			 * (horizontal front porch) or HBP (horizontal back porch) to
			 * resync with LTDC pixel clock.
			 */
			delay_to_lp = DIV_ROUND_UP((dphy_timing.data_hs2lp + dphy_timing.data_lp2hs)
						   * lanes * BITS_PER_BYTE, bpp);
			if (hfp < delay_to_lp && hbp < delay_to_lp)
				return MODE_HSYNC;
		}
	}

	return MODE_OK;
}

static const struct dw_mipi_dsi_phy_ops dw_mipi_dsi_stm_phy_ops = {
	.init = dw_mipi_dsi_phy_init,
	.power_on = dw_mipi_dsi_phy_power_on,
	.power_off = dw_mipi_dsi_phy_power_off,
	.get_lane_mbps = dw_mipi_dsi_get_lane_mbps,
	.get_timing = dw_mipi_dsi_phy_get_timing,
};

static const struct dw_mipi_dsi_phy_ops dw_mipi_dsi_stm_phy_141_ops = {
	.init = dw_mipi_dsi_phy_141_init,
	.power_on = dw_mipi_dsi_phy_power_on,
	.power_off = dw_mipi_dsi_phy_power_off,
	.get_lane_mbps = dw_mipi_dsi_phy_141_get_lane_mbps,
	.get_timing = dw_mipi_dsi_phy_141_get_timing,
};

static struct dw_mipi_dsi_plat_data dw_mipi_dsi_stm_plat_data = {
	.max_data_lanes = 2,
	.mode_valid = dw_mipi_dsi_stm_mode_valid,
	.phy_ops = &dw_mipi_dsi_stm_phy_ops,
};

static struct dw_mipi_dsi_plat_data dw_mipi_dsi_stm32mp25_plat_data = {
	.max_data_lanes = 4,
	.mode_valid = dw_mipi_dsi_stm_mode_valid,
	.phy_ops = &dw_mipi_dsi_stm_phy_141_ops,
};

static const struct of_device_id dw_mipi_dsi_stm_dt_ids[] = {
	{ .compatible = "st,stm32-dsi", .data = &dw_mipi_dsi_stm_plat_data, },
	{ .compatible = "st,stm32mp25-dsi", .data = &dw_mipi_dsi_stm32mp25_plat_data, },
	{ },
};
MODULE_DEVICE_TABLE(of, dw_mipi_dsi_stm_dt_ids);

static int dw_mipi_dsi_stm_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct dw_mipi_dsi_stm *dsi;
	const struct dw_mipi_dsi_plat_data *pdata = of_device_get_match_data(dev);
	int ret;

	dsi = devm_kzalloc(dev, sizeof(*dsi), GFP_KERNEL);
	if (!dsi)
		return -ENOMEM;

	dsi->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(dsi->base)) {
		ret = PTR_ERR(dsi->base);
		DRM_ERROR("Unable to get dsi registers %d\n", ret);
		return ret;
	}

	if (of_device_is_compatible(dev->of_node, "st,stm32-dsi")) {
		dsi->vdd_supply = devm_regulator_get(dev, "phy-dsi");
		if (IS_ERR(dsi->vdd_supply)) {
			ret = PTR_ERR(dsi->vdd_supply);
			dev_err_probe(dev, ret, "Failed to request regulator\n");
			return ret;
		}

		ret = regulator_enable(dsi->vdd_supply);
		if (ret) {
			DRM_ERROR("Failed to enable regulator vdd: %d\n", ret);
			return ret;
		}
	}

	if (of_device_is_compatible(dev->of_node, "st,stm32mp25-dsi")) {
		dsi->vdd_supply = devm_regulator_get(dev, "vdd");
		if (IS_ERR(dsi->vdd_supply)) {
			ret = PTR_ERR(dsi->vdd_supply);
			dev_err_probe(dev, ret, "Failed to request regulator\n");
			return ret;
		}

		ret = regulator_enable(dsi->vdd_supply);
		if (ret) {
			DRM_ERROR("Failed to enable regulator vdd: %d\n", ret);
			return ret;
		}

		dsi->vdda18_supply = devm_regulator_get(dev, "vdda18");
		if (IS_ERR(dsi->vdda18_supply)) {
			ret = PTR_ERR(dsi->vdda18_supply);
			dev_err_probe(dev, ret, "Failed to request regulator\n");
			goto err_clk_get;
		}

		ret = regulator_enable(dsi->vdda18_supply);
		if (ret) {
			DRM_ERROR("Failed to enable regulator vdda18: %d\n", ret);
			return ret;
		}
	}

	dsi->pllref_clk = devm_clk_get(dev, "ref");
	if (IS_ERR(dsi->pllref_clk)) {
		ret = PTR_ERR(dsi->pllref_clk);
		dev_err_probe(dev, ret, "Unable to get pll reference clock\n");
		goto err_regu_enable;
	}

	dsi->pclk = devm_clk_get(dev, "pclk");
	if (IS_ERR(dsi->pclk)) {
		ret = PTR_ERR(dsi->pclk);
		DRM_ERROR("Unable to get peripheral clock: %d\n", ret);
		goto err_regu_enable;
	}

	ret = clk_prepare_enable(dsi->pclk);
	if (ret) {
		DRM_ERROR("%s: Failed to enable peripheral clk\n", __func__);
		goto err_regu_enable;
	}

	dsi->hw_version = dsi_read(dsi, DSI_VERSION) & VERSION;

	if (dsi->hw_version != HWVER_130 &&
	    dsi->hw_version != HWVER_131 &&
	    dsi->hw_version != HWVER_141) {
		ret = -ENODEV;
		DRM_ERROR("bad dsi hardware version\n");
		goto err_dsi_probe;
	}

	/* Update lane capabilities according to hw version */
	switch (dsi->hw_version) {
	case HWVER_141:
		dsi->lane_min_kbps = LANE_MIN_PHY_141_KBPS;
		dsi->lane_max_kbps = LANE_MAX_PHY_141_KBPS;
	break;
	case HWVER_131:
		dsi->lane_min_kbps = 2 * LANE_MIN_KBPS;
		dsi->lane_max_kbps = 2 * LANE_MAX_KBPS;
	break;
	default:
		dsi->lane_min_kbps = LANE_MIN_KBPS;
		dsi->lane_max_kbps = LANE_MAX_KBPS;
	break;
	}

	dsi->pdata = *pdata;
	dsi->pdata.base = dsi->base;
	dsi->pdata.priv_data = dsi;

	platform_set_drvdata(pdev, dsi);

	dsi->dsi = dw_mipi_dsi_probe(pdev, &dsi->pdata);
	if (IS_ERR(dsi->dsi)) {
		ret = PTR_ERR(dsi->dsi);
		dev_err_probe(dev, ret, "Failed to initialize mipi dsi host\n");
		goto err_dsi_probe;
	}

	ret = dw_mipi_dsi_clk_register(dsi, dev);
	if (ret) {
		DRM_ERROR("Failed to register DSI pixel clock: %d\n", ret);
		goto err_dsi_probe;
	}

	dsi->px_clk = devm_clk_get(dev, "px_clk");
	/* No need to return since only MP25 has it */
	if (IS_ERR(dsi->px_clk))
		dev_err_probe(dev, PTR_ERR(dsi->px_clk), "Unable to get px_clk clock\n");

	dsi->probe_done = true;

	/*
	 * To obtain a continuous display after the probe, the txbyte clock must
	 * remain activated
	 */
	if (device_property_read_bool(dev, "default-on")) {
		ret = clk_prepare_enable(dsi->txbyte_clk.clk);
		if (ret) {
			DRM_ERROR("Failed to enable DSI pixel clock: %d\n", ret);
			goto err_dsi_probe;
		}
	}

	clk_disable_unprepare(dsi->pclk);
	regulator_disable(dsi->vdd_supply);

	return 0;

err_dsi_probe:
	clk_disable_unprepare(dsi->pclk);
err_regu_enable:
	if (dsi->vdda18_supply)
		regulator_disable(dsi->vdda18_supply);
err_clk_get:
	regulator_disable(dsi->vdd_supply);

	return ret;
}

static int dw_mipi_dsi_stm_remove(struct platform_device *pdev)
{
	struct dw_mipi_dsi_stm *dsi = platform_get_drvdata(pdev);

	dw_mipi_dsi_remove(dsi->dsi);
	dw_mipi_dsi_clk_unregister(dsi);

	return 0;
}

static int __maybe_unused dw_mipi_dsi_stm_suspend(struct device *dev)
{
	struct dw_mipi_dsi_stm *dsi = dev_get_drvdata(dev);

	DRM_DEBUG_DRIVER("\n");

	clk_disable_unprepare(dsi->pllref_clk);
	clk_disable_unprepare(dsi->pclk);
	regulator_disable(dsi->vdd_supply);
	if (dsi->vdda18_supply)
		regulator_disable(dsi->vdda18_supply);

	return 0;
}

static int __maybe_unused dw_mipi_dsi_stm_resume(struct device *dev)
{
	struct dw_mipi_dsi_stm *dsi = dev_get_drvdata(dev);
	int ret;

	DRM_DEBUG_DRIVER("\n");

	if (dsi->vdda18_supply) {
		ret = regulator_enable(dsi->vdda18_supply);
		if (ret) {
			DRM_ERROR("Failed to enable regulator vdda18: %d\n", ret);
			return ret;
		}
	}

	ret = regulator_enable(dsi->vdd_supply);
	if (ret) {
		if (dsi->vdda18_supply)
			regulator_disable(dsi->vdda18_supply);
		DRM_ERROR("Failed to enable regulator vdd: %d\n", ret);
		return ret;
	}

	ret = clk_prepare_enable(dsi->pclk);
	if (ret) {
		regulator_disable(dsi->vdd_supply);
		if (dsi->vdda18_supply)
			regulator_disable(dsi->vdda18_supply);
		DRM_ERROR("Failed to enable pclk: %d\n", ret);
		return ret;
	}

	ret = clk_prepare_enable(dsi->pllref_clk);
	if (ret) {
		clk_disable_unprepare(dsi->pclk);
		regulator_disable(dsi->vdd_supply);
		if (dsi->vdda18_supply)
			regulator_disable(dsi->vdda18_supply);
		DRM_ERROR("Failed to enable pllref_clk: %d\n", ret);
		return ret;
	}

	return 0;
}

static const struct dev_pm_ops dw_mipi_dsi_stm_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
	SET_RUNTIME_PM_OPS(dw_mipi_dsi_stm_suspend,
			   dw_mipi_dsi_stm_resume, NULL)
};

static struct platform_driver dw_mipi_dsi_stm_driver = {
	.probe		= dw_mipi_dsi_stm_probe,
	.remove		= dw_mipi_dsi_stm_remove,
	.driver		= {
		.of_match_table = dw_mipi_dsi_stm_dt_ids,
		.name	= "stm32-display-dsi",
		.pm = &dw_mipi_dsi_stm_pm_ops,
	},
};

module_platform_driver(dw_mipi_dsi_stm_driver);

MODULE_AUTHOR("Philippe Cornu <philippe.cornu@foss.st.com>");
MODULE_AUTHOR("Yannick Fertre <yannick.fertre@foss.st.com>");
MODULE_AUTHOR("Raphaël Gallais-Pou <raphael.gallais-pou@foss.st.com>");
MODULE_DESCRIPTION("STMicroelectronics DW MIPI DSI host controller driver");
MODULE_LICENSE("GPL v2");
