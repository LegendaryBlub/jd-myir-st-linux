/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) Maxime Coquelin 2015
 * Copyright (C) STMicroelectronics SA 2017
 * Authors:  Maxime Coquelin <mcoquelin.stm32@gmail.com>
 *	     Gerald Baeza <gerald_baeza@yahoo.fr>
 */

#define DRIVER_NAME "stm32-usart"

enum stm32_baudgen_type {
	UART_BAUDGEN = 0,
	LPUART_BAUDGEN = 1
};

struct stm32_usart_offsets {
	u16 cr1;
	u16 cr2;
	u16 cr3;
	u16 brr;
	u16 gtpr;
	u16 rtor;
	u16 rqr;
	u16 isr;
	u16 icr;
	u16 rdr;
	u16 tdr;
	u16 presc;
	u16 hwcfgr1;
};

struct stm32_backup_regs {
	u32 cr1;
	u32 cr2;
	u32 cr3;
	u32 gtpr;
	u32 brr;
};

struct stm32_usart_config {
	u8 uart_enable_bit; /* USART_CR1_UE */
	bool has_7bits_data;
	bool has_swap;
	bool has_wakeup;
	bool has_fifo;
};

struct stm32_usart_info {
	struct stm32_usart_offsets ofs;
	struct stm32_usart_config cfg;
};

#define UNDEF_REG 0xfff

/* USART_SR (F4) / USART_ISR (F7) */
#define USART_SR_PE		BIT(0)
#define USART_SR_FE		BIT(1)
#define USART_SR_NE		BIT(2)		/* F7 (NF for F4) */
#define USART_SR_ORE		BIT(3)
#define USART_SR_IDLE		BIT(4)
#define USART_SR_RXNE		BIT(5)
#define USART_SR_TC		BIT(6)
#define USART_SR_TXE		BIT(7)
#define USART_SR_CTSIF		BIT(9)
#define USART_SR_CTS		BIT(10)		/* F7 */
#define USART_SR_RTOF		BIT(11)		/* F7 */
#define USART_SR_EOBF		BIT(12)		/* F7 */
#define USART_SR_ABRE		BIT(14)		/* F7 */
#define USART_SR_ABRF		BIT(15)		/* F7 */
#define USART_SR_BUSY		BIT(16)		/* F7 */
#define USART_SR_CMF		BIT(17)		/* F7 */
#define USART_SR_SBKF		BIT(18)		/* F7 */
#define USART_SR_WUF		BIT(20)		/* H7 */
#define USART_SR_TEACK		BIT(21)		/* F7 */
#define USART_SR_ERR_MASK	(USART_SR_ORE | USART_SR_NE | USART_SR_FE |\
				 USART_SR_PE)
#define USART_SR_TCBGT		BIT(25)		/* F7 */
/* Dummy bits */
#define USART_SR_DUMMY_RX	BIT(16)

/* USART_DR */
#define USART_DR_MASK		GENMASK(8, 0)

/* USART_BRR */
#define USART_BRR_DIV_F_MASK	GENMASK(3, 0)
#define USART_BRR_DIV_M_MASK	GENMASK(15, 4)
#define USART_BRR_DIV_M_SHIFT	4
#define USART_BRR_04_R_SHIFT	1
#define USART_BRR_MASK		(USART_BRR_DIV_M_MASK | USART_BRR_DIV_F_MASK)

/* LPUART_BRR */
#define LPUART_BRR_MASK		GENMASK(19, 0)
#define LPUART_BRR_MIN_VALUE	0x300

/* USART_CR1 */
#define USART_CR1_SBK		BIT(0)
#define USART_CR1_RWU		BIT(1)		/* F4 */
#define USART_CR1_UESM		BIT(1)		/* H7 */
#define USART_CR1_RE		BIT(2)
#define USART_CR1_TE		BIT(3)
#define USART_CR1_IDLEIE	BIT(4)
#define USART_CR1_RXNEIE	BIT(5)
#define USART_CR1_TCIE		BIT(6)
#define USART_CR1_TXEIE		BIT(7)
#define USART_CR1_PEIE		BIT(8)
#define USART_CR1_PS		BIT(9)
#define USART_CR1_PCE		BIT(10)
#define USART_CR1_WAKE		BIT(11)
#define USART_CR1_M0		BIT(12)		/* F7 (CR1_M for F4) */
#define USART_CR1_MME		BIT(13)		/* F7 */
#define USART_CR1_CMIE		BIT(14)		/* F7 */
#define USART_CR1_OVER8		BIT(15)
#define USART_CR1_DEDT_MASK	GENMASK(20, 16)	/* F7 */
#define USART_CR1_DEAT_MASK	GENMASK(25, 21)	/* F7 */
#define USART_CR1_RTOIE		BIT(26)		/* F7 */
#define USART_CR1_EOBIE		BIT(27)		/* F7 */
#define USART_CR1_M1		BIT(28)		/* F7 */
#define USART_CR1_IE_MASK	(GENMASK(8, 4) | BIT(14) | BIT(26) | BIT(27))
#define USART_CR1_FIFOEN	BIT(29)		/* H7 */
#define USART_CR1_DEAT_SHIFT 21
#define USART_CR1_DEDT_SHIFT 16

/* USART_CR2 */
#define USART_CR2_ADD_MASK	GENMASK(3, 0)	/* F4 */
#define USART_CR2_ADDM7		BIT(4)		/* F7 */
#define USART_CR2_LBCL		BIT(8)
#define USART_CR2_CPHA		BIT(9)
#define USART_CR2_CPOL		BIT(10)
#define USART_CR2_CLKEN		BIT(11)
#define USART_CR2_STOP_2B	BIT(13)
#define USART_CR2_STOP_MASK	GENMASK(13, 12)
#define USART_CR2_LINEN		BIT(14)
#define USART_CR2_SWAP		BIT(15)		/* F7 */
#define USART_CR2_RXINV		BIT(16)		/* F7 */
#define USART_CR2_TXINV		BIT(17)		/* F7 */
#define USART_CR2_DATAINV	BIT(18)		/* F7 */
#define USART_CR2_MSBFIRST	BIT(19)		/* F7 */
#define USART_CR2_ABREN		BIT(20)		/* F7 */
#define USART_CR2_ABRMOD_MASK	GENMASK(22, 21)	/* F7 */
#define USART_CR2_RTOEN		BIT(23)		/* F7 */
#define USART_CR2_ADD_F7_MASK	GENMASK(31, 24)	/* F7 */

/* USART_CR3 */
#define USART_CR3_EIE		BIT(0)
#define USART_CR3_IREN		BIT(1)
#define USART_CR3_IRLP		BIT(2)
#define USART_CR3_HDSEL		BIT(3)
#define USART_CR3_NACK		BIT(4)
#define USART_CR3_SCEN		BIT(5)
#define USART_CR3_DMAR		BIT(6)
#define USART_CR3_DMAT		BIT(7)
#define USART_CR3_RTSE		BIT(8)
#define USART_CR3_CTSE		BIT(9)
#define USART_CR3_CTSIE		BIT(10)
#define USART_CR3_ONEBIT	BIT(11)
#define USART_CR3_OVRDIS	BIT(12)		/* F7 */
#define USART_CR3_DDRE		BIT(13)		/* F7 */
#define USART_CR3_DEM		BIT(14)		/* F7 */
#define USART_CR3_DEP		BIT(15)		/* F7 */
#define USART_CR3_SCARCNT_MASK	GENMASK(19, 17)	/* F7 */
#define USART_CR3_WUS_MASK	GENMASK(21, 20)	/* H7 */
#define USART_CR3_WUS_START_BIT	BIT(21)		/* H7 */
#define USART_CR3_WUFIE		BIT(22)		/* H7 */
#define USART_CR3_TXFTIE	BIT(23)		/* H7 */
#define USART_CR3_TCBGTIE	BIT(24)		/* H7 */
#define USART_CR3_RXFTCFG_MASK	GENMASK(27, 25)	/* H7 */
#define USART_CR3_RXFTCFG_SHIFT	25		/* H7 */
#define USART_CR3_RXFTIE	BIT(28)		/* H7 */
#define USART_CR3_TXFTCFG_MASK	GENMASK(31, 29)	/* H7 */
#define USART_CR3_TXFTCFG_SHIFT	29		/* H7 */

/* USART_GTPR */
#define USART_GTPR_PSC_SMART_MASK	GENMASK(4, 0)
#define USART_GTPR_PSC_MASK	GENMASK(7, 0)
#define USART_GTPR_GT_MASK	GENMASK(15, 8)

/* USART_RTOR */
#define USART_RTOR_RTO_MASK	GENMASK(23, 0)	/* F7 */
#define USART_RTOR_BLEN_MASK	GENMASK(31, 24)	/* F7 */

/* USART_RQR */
#define USART_RQR_ABRRQ		BIT(0)		/* F7 */
#define USART_RQR_SBKRQ		BIT(1)		/* F7 */
#define USART_RQR_MMRQ		BIT(2)		/* F7 */
#define USART_RQR_RXFRQ		BIT(3)		/* F7 */
#define USART_RQR_TXFRQ		BIT(4)		/* F7 */

/* USART_ICR */
#define USART_ICR_PECF		BIT(0)		/* F7 */
#define USART_ICR_FECF		BIT(1)		/* F7 */
#define USART_ICR_ORECF		BIT(3)		/* F7 */
#define USART_ICR_IDLECF	BIT(4)		/* F7 */
#define USART_ICR_TCCF		BIT(6)		/* F7 */
#define USART_ICR_TCBGTCF	BIT(7)		/* F7 */
#define USART_ICR_CTSCF		BIT(9)		/* F7 */
#define USART_ICR_RTOCF		BIT(11)		/* F7 */
#define USART_ICR_EOBCF		BIT(12)		/* F7 */
#define USART_ICR_CMCF		BIT(17)		/* F7 */
#define USART_ICR_WUCF		BIT(20)		/* H7 */

/* USART_PRESC */
#define USART_PRESC		GENMASK(3, 0)	/* H7 */
#define USART_PRESC_MAX		0b1011
static const unsigned int STM32_USART_PRESC_VAL[] = {1, 2, 4, 6, 8, 10, 12, 16, 32, 64, 128, 256};

/* USART_HWCFCR1 */
#define USART_HWCFCR1_CFG4	GENMASK(15, 12)	/* MP1 */
#define USART_HWCFCR1_CFG7	GENMASK(27, 24)	/* MP1 */
#define USART_HWCFCR1_CFG8	GENMASK(31, 28)	/* MP1 */

#define STM32_SERIAL_NAME "ttySTM"
#define STM32_MAX_PORTS 10
#define STM32H7_USART_FIFO_SIZE 16

#define RX_BUF_L 4096		 /* dma rx buffer length     */
#define RX_BUF_P (RX_BUF_L / 2)	 /* dma rx buffer period     */
#define TX_BUF_L RX_BUF_L	 /* dma tx buffer length     */

#define STM32_USART_TIMEOUT_USEC USEC_PER_SEC /* 1s timeout in µs */

#define LPUART_RECEIVE_TIMEOUT_MS 1000

struct stm32_port {
	struct uart_port port;
	struct clk *clk;
	const struct stm32_usart_info *info;
	struct dma_chan *rx_ch;  /* dma rx channel            */
	dma_addr_t rx_dma_buf;   /* dma rx buffer bus address */
	unsigned char *rx_buf;   /* dma rx buffer cpu address */
	struct dma_chan *tx_ch;  /* dma tx channel            */
	dma_addr_t tx_dma_buf;   /* dma tx buffer bus address */
	unsigned char *tx_buf;   /* dma tx buffer cpu address */
	u32 cr1_irq;		 /* USART_CR1_RXNEIE or RTOIE */
	u32 cr3_irq;		 /* USART_CR3_RXFTIE */
	int last_res;
	bool tx_dma_busy;	 /* dma tx transaction in progress */
	bool rx_dma_busy;	 /* dma rx transaction in progress */
	bool throttled;		 /* port throttled            */
	bool hw_flow_control;
	bool swap;		 /* swap RX & TX pins */
	bool fifoen;
	bool txdone;
	bool has_rtor;
	bool has_smartcard;
	int rxftcfg;		/* RX FIFO threshold CFG      */
	int txftcfg;		/* TX FIFO threshold CFG      */
	bool wakeup_src;
	int rdr_mask;		/* receive data register mask */
	struct mctrl_gpios *gpios; /* modem control gpios */
	struct dma_tx_state rx_dma_state;
	struct stm32_backup_regs bkp_regs;
	enum stm32_baudgen_type baudgen;
	struct timer_list rx_dma_timer; /* Only used for LPUART */
};

static struct stm32_port stm32_ports[STM32_MAX_PORTS];
static struct uart_driver stm32_usart_driver;
