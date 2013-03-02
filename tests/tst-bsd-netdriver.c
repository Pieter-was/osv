#include <stdio.h>

#include <bsd/porting/netport.h>
#include <bsd/porting/networking.h>
#include <bsd/porting/route.h>

#include <bsd/sys/net/if_var.h>
#include <bsd/sys/net/if.h>
#include <bsd/sys/net/if_arp.h>
#include <bsd/sys/net/ethernet.h>
#include <bsd/sys/net/if_dl.h>
#include <bsd/sys/net/if_types.h>
#include <bsd/sys/netinet/in.h>
#include <bsd/sys/netinet/in_var.h>
#include <bsd/sys/netinet/ip.h>
#include <bsd/sys/netinet/ip_icmp.h>
#include <bsd/sys/sys/sockio.h>
#include <bsd/sys/sys/socket.h>
#include <bsd/sys/sys/socketvar.h>
#include <bsd/machine/in_cksum.h>

/* Test log */
#define TLOG(...) printf(__VA_ARGS__)

/* Global ifnet */
struct ifnet* pifp;

static u_char if_eaddr[ETHER_ADDR_LEN] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
static char *if_eaddr_cstr = "11:22:33:44:55:66";
static char *gw_eaddr_cstr = "77:22:33:44:55:66";
static char *if_name = "tst-netdriver";
static char *if_name1 = "tst-netdriver0";
static char *if_ip = "198.0.0.4";
static char *if_gw = "198.0.0.1";
static char *if_baddr = "198.0.0.255";
static int masklen = 24;

/*
 * This function should invoke ether_ioctl...
 */
static int
lge_ioctl(struct ifnet        *ifp,
          u_long          command,
          caddr_t         data)
{
    TLOG("lge_ioctl(%x)\n", command);

    int error = 0;
    switch(command) {
    case SIOCSIFMTU:
        TLOG("SIOCSIFMTU\n");
        break;
    case SIOCSIFFLAGS:
        TLOG("SIOCSIFFLAGS\n");
        /* Change status ifup, ifdown */
        if (ifp->if_flags & IFF_UP) {
            ifp->if_drv_flags |= IFF_DRV_RUNNING;
            TLOG("if_up\n");
        } else {
            ifp->if_drv_flags &= ~IFF_DRV_RUNNING;
            TLOG("if_down\n");
        }
        break;
    case SIOCADDMULTI:
    case SIOCDELMULTI:
        TLOG("SIOCDELMULTI\n");
        break;
    default:
        TLOG("redirecting to ether_ioctl()...\n");
        error = ether_ioctl(ifp, command, data);
        break;
    }

    return(error);
}

/*
 * Main transmit routine.
 */
static void
lge_start(struct ifnet* ifp)
{
    struct mbuf     *m_head = NULL;

    TLOG("lge_start (transmit)\n");

    IF_DEQUEUE(&ifp->if_snd, m_head);
    if (m_head != NULL) {
        TLOG("*** processing packet! ***\n");
    }
}

static void
lge_init(void *xsc)
{
    TLOG("lge_init\n");
}

int create_if(void)
{
    printf("[~] Creating interface!\n");
    pifp = if_alloc(IFT_ETHER);
    if (pifp == NULL) {
        printf("[-] if_alloc() failed!\n");
        return (-1);
    }

    if_initname(pifp, if_name, 0);
    pifp->if_mtu = ETHERMTU;
    pifp->if_softc = (void*)"Driver private softc";
    pifp->if_flags = IFF_BROADCAST /*| IFF_MULTICAST*/;
    pifp->if_ioctl = lge_ioctl;
    pifp->if_start = lge_start;
    pifp->if_init = lge_init;
    pifp->if_snd.ifq_maxlen = 2;
    pifp->if_capabilities = 0/* IFCAP_RXCSUM */;
    pifp->if_capenable = pifp->if_capabilities;

    ether_ifattach(pifp, if_eaddr);

    return (0);
}

void destroy_if(void)
{
    ether_ifdetach(pifp);
    if_free(pifp);
}

void test_sockets(void)
{
    /* ICMP Packet */
    struct mbuf *m;
    struct icmp *icp;
    char *raw;
    char *echo_payload = "ABCDEFGHIJ";

    /* Socket Variables */
    struct socket *s;
    struct sockaddr whereto;
    struct sockaddr_in *to;

    /* Create socket */
    socreate(AF_INET, &s, SOCK_RAW, IPPROTO_ICMP, NULL, NULL);

    /* Setup address */
    memset(&whereto, 0, sizeof(struct sockaddr));
    whereto.sa_len = sizeof(struct sockaddr);
    to = (struct sockaddr_in *)&whereto;
    to->sin_family = AF_INET;
    inet_aton(if_gw, &to->sin_addr);

    /* ICMP ECHO Packet */
    m = m_getcl(M_DONTWAIT, MT_DATA, M_PKTHDR);
    m->m_pkthdr.len = m->m_len = ICMP_MINLEN + 10;
    icp = mtod(m, struct icmp *);
    icp->icmp_type = ICMP_ECHO;
    icp->icmp_code = 0;
    icp->icmp_cksum = 0;
    icp->icmp_id = 0xAABB;
    icp->icmp_seq = 0;
    raw = mtod(m, char *);
    raw += ICMP_MINLEN;
    bcopy(echo_payload, raw, 10);
    icp->icmp_cksum = in_cksum(m, 18);

    /* Send an ICMP packet on our interface */
    sosend_dgram(s, &whereto, NULL, m, NULL, 0, NULL);

    soclose(s);
}

int main(void)
{
    TLOG("BSD Net Driver Test BEGIN\n");

    create_if();

    osv_start_if(if_name1, if_ip, if_baddr, masklen);
    osv_ifup(if_name1);

    /* Add ARP */
    osv_route_arp_add(if_name1, if_ip, if_eaddr_cstr);
    osv_route_arp_add(if_name1, if_gw, gw_eaddr_cstr);

    /* Add route */
    osv_route_add_host(if_ip, if_gw);

    /* Send ICMP Packet */
    test_sockets();
    destroy_if();

    TLOG("BSD Net Driver Test END\n");
    return (0);
}

#undef TLOG