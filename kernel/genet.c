// The Raspberry Pi 4's Ethernet: the BCM2711's GENET v5 MAC
// ("brcm,bcm2711-genet-v5") and its BCM54213PE PHY, over RGMII. Set up
// as U-Boot's bcmgenet.c does, with the PHY's clock delays as Linux
// sets them for "rgmii-rxid". One DMA ring each way (the default queue,
// 16) of 256 descriptors, which live in the MAC; frames go to and come
// from DMA buffers (dma.c), 2KB each. A kernel thread ("genet") brings
// the link up, then polls for received frames every tick, as USB does,
// and watches the link; DHCP gets the addresses.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "defs.h"
#include "console.h"
#include "spinlock.h"
#include "proc.h"
#include "timer.h"
#include "fdt.h"
#include "vm.h"
#include "mmu.h"
#include "socket.h"
#include "net.h"

#define SYS_REV_CTRL        0x0000
#define SYS_PORT_CTRL       0x0004
#define PORT_MODE_EXT_GPHY  3
#define SYS_RBUF_FLUSH_CTRL 0x0008
#define EXT_RGMII_OOB_CTRL  0x008c
#define RGMII_LINK          (1 << 4)
#define OOB_DISABLE         (1 << 5)
#define RGMII_MODE_EN       (1 << 6)
#define ID_MODE_DIS         (1 << 16)
#define RBUF_CTRL           0x0300
#define RBUF_ALIGN_2B       (1 << 1)
#define RBUF_TBUF_SIZE_CTRL 0x03b4
#define UMAC_CMD            0x0808
#define CMD_TX_EN           (1 << 0)
#define CMD_RX_EN           (1 << 1)
#define CMD_SPEED_SHIFT     2          // 0: 10, 1: 100, 2: 1000 Mbit/s
#define CMD_SW_RESET        (1 << 13)
#define CMD_LCL_LOOP_EN     (1 << 15)
#define UMAC_MAC0           0x080c
#define UMAC_MAC1           0x0810
#define UMAC_MAX_FRAME_LEN  0x0814
#define UMAC_TX_FLUSH       0x0b34
#define UMAC_MIB_CTRL       0x0d80
#define MIB_RESET_RX        (1 << 0)
#define MIB_RESET_RUNT      (1 << 1)
#define MIB_RESET_TX        (1 << 2)
#define MDIO_CMD            0x0e14
#define MDIO_START_BUSY     (1 << 29)
#define MDIO_READ_FAIL      (1 << 28)
#define MDIO_RD             (2 << 26)
#define MDIO_WR             (1 << 26)
#define MDIO_PMD_SHIFT      21
#define MDIO_REG_SHIFT      16

// DMA: the descriptors, then the rings' registers, then the DMA's
#define NDESC        256
#define DESC_SIZE    12      // length/status, address low, address high
#define DEFAULT_Q    16
#define RX_OFF       0x2000
#define TX_OFF       0x4000
#define RDMA_REGS    (RX_OFF + NDESC * DESC_SIZE)
#define TDMA_REGS    (TX_OFF + NDESC * DESC_SIZE)
#define RING_SIZE    0x40
#define RDMA_RING    (RDMA_REGS + DEFAULT_Q * RING_SIZE)
#define TDMA_RING    (TDMA_REGS + DEFAULT_Q * RING_SIZE)
#define RDMA_CTRLS   (RDMA_REGS + (DEFAULT_Q + 1) * RING_SIZE)
#define TDMA_CTRLS   (TDMA_REGS + (DEFAULT_Q + 1) * RING_SIZE)
// in a ring's registers
#define RDMA_WRITE_PTR   0x00
#define TDMA_READ_PTR    0x00
#define RDMA_PROD_INDEX  0x08
#define TDMA_CONS_INDEX  0x08
#define RDMA_CONS_INDEX  0x0c
#define TDMA_PROD_INDEX  0x0c
#define RING_BUF_SIZE    0x10
#define RING_START_ADDR  0x14
#define RING_END_ADDR    0x1c
#define TDMA_MBUF_DONE   0x24
#define RDMA_XON_XOFF    0x28
#define TDMA_FLOW_PERIOD 0x28
#define RDMA_READ_PTR    0x2c
#define TDMA_WRITE_PTR   0x2c
// in the DMA's
#define DMA_RING_CFG     0x00
#define DMA_CTRL         0x04
#define DMA_SCB_BURST    0x0c
#define DMA_EN           (1 << 0)
// descriptor bits
#define DESC_OWN         0x8000
#define DESC_EOP         0x4000
#define DESC_SOP         0x2000
#define DESC_RX_ERRORS   0x001f   // overrun, CRC, receive, no buffer, length
#define DESC_TX_APPEND_CRC 0x0040
#define DESC_TX_QTAG     (0x3f << 7)

#define BUFLEN       2048
#define RX_ALIGN     2     // RBUF_ALIGN_2B: two bytes before each frame
#define MAX_FRAME    1536

#define PHY_ADDR_DEFAULT 1
// PHY registers
#define MII_BMCR     0
#define MII_BMSR     1
#define MII_ANAR     4
#define MII_LPA      5
#define MII_CTRL1000 9
#define MII_STAT1000 10
#define BMSR_LINK    (1 << 2)
#define BMSR_ANDONE  (1 << 5)
// Broadcom's: the aux control's MISC shadow, and the 0x1c shadows
#define BCM_AUXCTL       0x18
#define BCM_SHADOW       0x1c
#define AUXCTL_MISC      0x7
#define AUXCTL_MISC_WREN 0x8000
#define AUXCTL_RGMII_SKEW 0x0100   // RXC-RXD skew: the PHY delays RX
#define SHD_CLK_CTL      0x3
#define SHD_GTXCLK_EN    0x0200    // the PHY delays TX

#define TAG_GET_MAC 0x00010003

static uint64 base;
static int phy = PHY_ADDR_DEFAULT;
static struct spinlock txlock = { .name = "genet" };
static char *rxbuf[NDESC], *txbuf[NDESC];
static uint32 rx_cons, rx_index, tx_prod, tx_index;

static uint32 rd(int r)
{
    return *(volatile uint32 *)(P2V(base) + r);
}

static void wr(int r, uint32 v)
{
    *(volatile uint32 *)(P2V(base) + r) = v;
}

static void udelay(uint64 us)
{
    for (uint64 t0 = timer_ns(); timer_ns() - t0 < us * 1000; )
        ;
}

// ---- the PHY, over MDIO

static int mdio_wait(void)
{
    for (int i = 0; i < 1000; i++) {
        if (!(rd(MDIO_CMD) & MDIO_START_BUSY))
            return 0;
        udelay(20);
    }
    return -1;
}

static int phy_read(int reg)
{
    wr(MDIO_CMD, MDIO_RD | phy << MDIO_PMD_SHIFT | reg << MDIO_REG_SHIFT);
    wr(MDIO_CMD, rd(MDIO_CMD) | MDIO_START_BUSY);
    if (mdio_wait() < 0)
        return -1;
    uint32 v = rd(MDIO_CMD);
    return v & MDIO_READ_FAIL ? -1 : (int)(v & 0xffff);
}

static void phy_write(int reg, int val)
{
    wr(MDIO_CMD, MDIO_WR | phy << MDIO_PMD_SHIFT | reg << MDIO_REG_SHIFT | (val & 0xffff));
    wr(MDIO_CMD, rd(MDIO_CMD) | MDIO_START_BUSY);
    mdio_wait();
}

// RGMII clock delays for "rgmii-rxid", as Linux's bcm54xx driver sets
// them: the PHY delays the receive clock, not the transmit clock (the
// MAC does that: its ID mode stays on).
static void phy_delays(void)
{
    phy_write(BCM_AUXCTL, AUXCTL_MISC | AUXCTL_MISC << 12);   // select MISC to read
    int v = phy_read(BCM_AUXCTL);
    if (v >= 0)
        phy_write(BCM_AUXCTL, (v | AUXCTL_MISC_WREN | AUXCTL_RGMII_SKEW) | AUXCTL_MISC);
    phy_write(BCM_SHADOW, (SHD_CLK_CTL & 0x1f) << 10);
    v = phy_read(BCM_SHADOW);
    if (v >= 0)
        phy_write(BCM_SHADOW, 0x8000 | (SHD_CLK_CTL & 0x1f) << 10 | ((v & ~SHD_GTXCLK_EN) & 0x3ff));
}

// Reset the PHY and start autonegotiation (10, 100, 1000, full and half).
static void phy_start(void)
{
    phy_write(MII_BMCR, 0x8000);   // reset
    for (int i = 0; i < 100 && (phy_read(MII_BMCR) & 0x8000); i++)
        udelay(1000);
    phy_delays();
    phy_write(MII_ANAR, 0x01e1);
    phy_write(MII_CTRL1000, 0x0300);
    phy_write(MII_BMCR, 0x1200);   // autonegotiate, restart
}

// Is the link up? Its speed in *speed (0: 10, 1: 100, 2: 1000).
static int phy_link(int *speed)
{
    phy_read(MII_BMSR);   // the link bit latches low: read twice
    int s = phy_read(MII_BMSR);
    if (s < 0 || !(s & BMSR_LINK) || !(s & BMSR_ANDONE))
        return 0;
    int c1000 = phy_read(MII_CTRL1000), s1000 = phy_read(MII_STAT1000);
    int lpa = phy_read(MII_LPA), anar = phy_read(MII_ANAR);
    if ((s1000 & 0x0c00) && (c1000 & 0x0300))
        *speed = 2;
    else if ((lpa & anar) & 0x0180)
        *speed = 1;
    else
        *speed = 0;
    return 1;
}

// ---- the MAC and its DMA

static void umac_reset(void)
{
    wr(SYS_RBUF_FLUSH_CTRL, rd(SYS_RBUF_FLUSH_CTRL) | (1 << 1));
    udelay(10);
    wr(SYS_RBUF_FLUSH_CTRL, rd(SYS_RBUF_FLUSH_CTRL) & ~(1 << 1));
    udelay(10);
    wr(SYS_RBUF_FLUSH_CTRL, 0);
    udelay(10);
    wr(UMAC_CMD, 0);
    wr(UMAC_CMD, CMD_SW_RESET | CMD_LCL_LOOP_EN);
    udelay(2);
    wr(UMAC_CMD, 0);
    wr(UMAC_MIB_CTRL, MIB_RESET_RX | MIB_RESET_TX | MIB_RESET_RUNT);
    wr(UMAC_MIB_CTRL, 0);
    wr(UMAC_MAX_FRAME_LEN, MAX_FRAME);
    wr(RBUF_CTRL, rd(RBUF_CTRL) | RBUF_ALIGN_2B);
    wr(RBUF_TBUF_SIZE_CTRL, 1);
}

static void rx_init(void)
{
    wr(RDMA_CTRLS + DMA_SCB_BURST, 8);
    wr(RDMA_RING + RING_START_ADDR, 0);
    wr(RDMA_RING + RDMA_READ_PTR, 0);
    wr(RDMA_RING + RDMA_WRITE_PTR, 0);
    wr(RDMA_RING + RING_END_ADDR, NDESC * DESC_SIZE / 4 - 1);
    // the producer index can't be set to 0: start the consumer at it
    rx_cons = rd(RDMA_RING + RDMA_PROD_INDEX) & 0xffff;
    wr(RDMA_RING + RDMA_CONS_INDEX, rx_cons);
    rx_index = rx_cons & 0xff;
    wr(RDMA_RING + RING_BUF_SIZE, NDESC << 16 | BUFLEN);
    wr(RDMA_RING + RDMA_XON_XOFF, 5 << 16 | NDESC >> 4);
    wr(RDMA_CTRLS + DMA_RING_CFG, 1 << DEFAULT_Q);
    for (int i = 0; i < NDESC; i++) {
        int d = RX_OFF + i * DESC_SIZE;
        wr(d + 4, dma_pa(rxbuf[i]));
        wr(d + 8, dma_pa(rxbuf[i]) >> 32);
        wr(d, BUFLEN << 16 | DESC_OWN);
    }
}

static void tx_init(void)
{
    wr(TDMA_CTRLS + DMA_SCB_BURST, 8);
    wr(TDMA_RING + RING_START_ADDR, 0);
    wr(TDMA_RING + TDMA_READ_PTR, 0);
    wr(TDMA_RING + TDMA_WRITE_PTR, 0);
    wr(TDMA_RING + RING_END_ADDR, NDESC * DESC_SIZE / 4 - 1);
    // the consumer index can't be set to 0: start the producer at it
    tx_prod = rd(TDMA_RING + TDMA_CONS_INDEX) & 0xffff;
    wr(TDMA_RING + TDMA_PROD_INDEX, tx_prod);
    tx_index = tx_prod & 0xff;
    wr(TDMA_RING + TDMA_MBUF_DONE, 1);
    wr(TDMA_RING + TDMA_FLOW_PERIOD, 0);
    wr(TDMA_RING + RING_BUF_SIZE, NDESC << 16 | BUFLEN);
    wr(TDMA_CTRLS + DMA_RING_CFG, 1 << DEFAULT_Q);
}

// Send an Ethernet frame; doesn't wait. Returns 0, or -1 (dropped).
static int genet_send(const void *frame, int len)
{
    if (len > MAX_FRAME - 4)
        return -1;
    acquire(&txlock);
    uint32 cons = rd(TDMA_RING + TDMA_CONS_INDEX) & 0xffff;
    if (((tx_prod - cons) & 0xffff) >= NDESC - 1) {
        release(&txlock);
        return -1;   // every buffer in flight
    }
    char *b = txbuf[tx_index];
    memmove(b, frame, len);
    if (len < 60) {
        memset(b + len, 0, 60 - len);   // the MAC doesn't pad short frames
        len = 60;
    }
    int d = TX_OFF + tx_index * DESC_SIZE;
    wr(d + 4, dma_pa(b));
    wr(d + 8, dma_pa(b) >> 32);
    dma_wmb();
    wr(d, len << 16 | DESC_TX_QTAG | DESC_TX_APPEND_CRC | DESC_SOP | DESC_EOP);
    tx_index = (tx_index + 1) % NDESC;
    tx_prod = (tx_prod + 1) & 0xffff;
    dma_wmb();
    wr(TDMA_RING + TDMA_PROD_INDEX, tx_prod);
    release(&txlock);
    return 0;
}

// Hand what came to the network stack. Returns how many frames.
static int receive(void)
{
    uint32 prod = rd(RDMA_RING + RDMA_PROD_INDEX) & 0xffff;
    int n = 0;
    while (rx_cons != prod) {
        uint32 ls = rd(RX_OFF + rx_index * DESC_SIZE);
        int len = (ls >> 16) & 0xfff;
        if (!(ls & DESC_RX_ERRORS) && (ls & DESC_SOP) && (ls & DESC_EOP) && len > RX_ALIGN)
            net_rx(rxbuf[rx_index] + RX_ALIGN, len - RX_ALIGN);
        rx_cons = (rx_cons + 1) & 0xffff;
        wr(RDMA_RING + RDMA_CONS_INDEX, rx_cons);
        rx_index = (rx_index + 1) % NDESC;
        n++;
    }
    return n;
}

static void link_up(int speed)
{
    static const char *mbit[] = { "10", "100", "1000" };
    wr(EXT_RGMII_OOB_CTRL, (rd(EXT_RGMII_OOB_CTRL) & ~OOB_DISABLE) | RGMII_LINK);
    wr(UMAC_CMD, speed << CMD_SPEED_SHIFT | CMD_TX_EN | CMD_RX_EN);
    printf("genet: link up, %s Mbit/s\n", mbit[speed]);
}

static void genet_thread(void *arg)
{
    phy_start();
    int up = 0, speed = 0, dhcp = 0;
    for (uint64 t = 0; ; t++) {
        if (t % HZ == 0) {
            // the link, once a second
            int s, now = phy_link(&s);
            if (now && (!up || s != speed)) {
                speed = s;
                link_up(speed);
                if (!dhcp) {
                    dhcp_start();
                    dhcp = 1;
                }
            } else if (!now && up) {
                printf("genet: link down\n");
            }
            up = now;
        }
        if (receive() == 0)
            sleep_ticks(1);
    }
}

void genet_init(void)
{
    int n = fdt_find_compatible(-1, "brcm,bcm2711-genet-v5");
    uint64 size;
    if (n < 0 || fdt_reg(n, 0, &base, &size) < 0 || netif.send)
        return;
    // its PHY's address: the first ethernet-phy node's reg
    for (int c = fdt_next_node(n, 0); c >= 0 && fdt_parent(fdt_parent(c)) == n;
         c = fdt_next_node(c, 0)) {
        uint64 a, s;
        if (strncmp(fdt_name(c), "ethernet-phy", 12) == 0 && fdt_reg(c, 0, &a, &s) == 0) {
            phy = a;
            break;
        }
    }
    kvmmap(kernel_pagetable, P2V(base), base, size, PTE_KERN_DEVICE);
    uint32 rev = rd(SYS_REV_CTRL);
    if (((rev >> 24) & 0xf) != 6) {   // GENET v5 says 6
        printf("genet: unknown revision %x\n", rev);
        return;
    }

    // the MAC address the firmware knows
    uint32 m[8] = { sizeof(m), 0, TAG_GET_MAC, 6, 0, 0, 0, 0 };
    uchar mac[6];
    if (rpi_mbox_call(m, NELEM(m)) < 0) {
        printf("genet: no MAC address\n");
        return;
    }
    memmove(mac, &m[5], 6);

    for (int i = 0; i < NDESC; i += 2) {
        char *p = dma_alloc(), *q = dma_alloc();
        if (p == 0 || q == 0)
            return;
        rxbuf[i] = p;
        rxbuf[i + 1] = p + BUFLEN;
        txbuf[i] = q;
        txbuf[i + 1] = q + BUFLEN;
    }

    wr(SYS_PORT_CTRL, PORT_MODE_EXT_GPHY);
    // RGMII on, the MAC's transmit delay (ID mode) on: "rgmii-rxid"
    wr(EXT_RGMII_OOB_CTRL, (rd(EXT_RGMII_OOB_CTRL) & ~ID_MODE_DIS) | RGMII_MODE_EN);
    umac_reset();
    wr(UMAC_MAC0, mac[0] << 24 | mac[1] << 16 | mac[2] << 8 | mac[3]);
    wr(UMAC_MAC1, mac[4] << 8 | mac[5]);
    // DMA off, flush, rings, DMA on
    wr(TDMA_CTRLS + DMA_CTRL, rd(TDMA_CTRLS + DMA_CTRL) & ~DMA_EN);
    wr(RDMA_CTRLS + DMA_CTRL, rd(RDMA_CTRLS + DMA_CTRL) & ~DMA_EN);
    wr(UMAC_TX_FLUSH, 1);
    udelay(10);
    wr(UMAC_TX_FLUSH, 0);
    rx_init();
    tx_init();
    uint32 ctrl = 1 << (DEFAULT_Q + 1) | DMA_EN;
    wr(TDMA_CTRLS + DMA_CTRL, ctrl);
    wr(RDMA_CTRLS + DMA_CTRL, rd(RDMA_CTRLS + DMA_CTRL) | ctrl);

    // no addresses until DHCP's
    net_ip = net_gateway = net_dns = 0;
    netif_register("genet", mac, genet_send);
    printf("genet: v5, phy %d, mac %x:%x:%x:%x:%x:%x\n", phy, mac[0], mac[1], mac[2], mac[3],
           mac[4], mac[5]);
    struct proc *p = kthread_create(genet_thread, 0);
    if (p)
        safestrcpy(p->name, "genet", sizeof(p->name));
}
