#include "network_lwip.h"
#include "init_function.h"
#include <stdlib.h>
#include <string.h>
extern "C" {
#include "dump_hex.h"
#include "FreeRTOS.h"
#include "FreeRTOSConfig.h"
#include "task.h"
#include "ftpd.h"


void echo_task(void *a);
}

/**
 * Initialization
 */
void initLwipCallback(void *a)
{
	printf("Lwip TCP init callback.\n");
	ftpd_init();
	printf("FTPDaemon initialized\n");
}

void initLwip(void *a, void *b)
{
	printf("Initializing lwIP.\n");
	tcpip_init(initLwipCallback, NULL);
	printf("Starting network services.\n");
	xTaskCreate( echo_task, "Echo task", configMINIMAL_STACK_SIZE, NULL, 2, NULL );
}


InitFunction lwIP_initializer(initLwip, NULL, NULL);

/**
 * Callbacks
 */
err_t lwip_init_callback(struct netif *netif)
{
    return ERR_OK;
}

static BYTE temporary_out_buffer[1536];

err_t lwip_output_callback(struct netif *netif, struct pbuf *pbuf)
{
    NetworkLWIP *ni = (NetworkLWIP *)netif->state;
    if (pbuf->len == pbuf->tot_len) {
    	return ni->driver_output_function(ni->driver, (BYTE *)pbuf->payload, pbuf->len);
    }
    if (pbuf->next == NULL) {
    	return ERR_BUF;
    }
    BYTE *temp = temporary_out_buffer;
    int total = pbuf->tot_len;
    if (total > 1536) {
    	return ERR_ARG;
    }
    while(pbuf) {
    	memcpy(temp, pbuf->payload, pbuf->len);
    	temp += pbuf->len;
    	if (pbuf->len == pbuf->tot_len)
    		break;
    	pbuf = pbuf->next;
    }
    return ni->driver_output_function(ni->driver, temporary_out_buffer, total);
}

void lwip_free_callback(void *p)
{
	struct pbuf_custom *pbuf = (struct pbuf_custom *)p;
	NetworkLWIP *ni = (NetworkLWIP *)pbuf->custom_obj;
    ni->driver_free_function(ni->driver, pbuf->buffer_start);
    ni->free_pbuf(pbuf);
}

/**
 * Factory
 */
NetworkInterface *getNetworkStack(void *driver,
								  driver_output_function_t out,
								  driver_free_function_t free)
{
	return new NetworkLWIP (driver, out, free);
}

void releaseNetworkStack(void *netstack)
{
	delete ((NetworkLWIP *)netstack);
}

/**
 * Class / instance functions
 */
NetworkLWIP :: NetworkLWIP(void *driver,
							driver_output_function_t out,
							driver_free_function_t free) : pbuf_fifo(64, NULL)
{
	this->driver = driver;
	this->driver_free_function = free;
	this->driver_output_function = out;
	if_up = false;
	for(int i=0;i<63;i++) {
		pbuf_fifo.push(&pbuf_array[i]);
	}
}

NetworkLWIP :: ~NetworkLWIP()
{
    dhcp_stop(&my_net_if);
	netif_set_down(&my_net_if);
	netif_remove(&my_net_if);
}

void ethernet_init_inside_thread(void *classPointer)
{
	((NetworkLWIP *)classPointer)->init_callback();
}

bool NetworkLWIP :: start()
{
	memset(&my_net_if, 0, sizeof(my_net_if));
	init_callback();
    if_up = false;
    return true;
}

void NetworkLWIP :: stop()
{
    //if_up = false;
}

void NetworkLWIP :: poll()
{
    if (netif_is_up(&my_net_if) && !if_up) {
        //printf("**** NETIF IS NOW UP ****\n");
        if_up = true;
    } else if(!netif_is_up(&my_net_if) && if_up) {
        //printf("#### NETIF IS NOW DOWN ####\n");
        if_up = false;
    }
}


void NetworkLWIP :: init_callback( )
{
	/* initialization of IP addresses */
	IP4_ADDR(&my_ip, 0,0,0,0);
    IP4_ADDR(&my_netmask, 0,0,0,0);
    IP4_ADDR(&my_gateway, 0,0,0,0);

	/* reset */
	my_net_if.state = NULL;
	my_net_if.next = NULL;

	/* set name */
	my_net_if.name[0] = 'U';
	my_net_if.name[1] = '2';

	/* set MAC hardware address length */
	my_net_if.hwaddr_len = ETHARP_HWADDR_LEN;
  
    /* set MAC hardware address */
	my_net_if.hwaddr[0] = mac_address[0];
	my_net_if.hwaddr[1] = mac_address[1];
	my_net_if.hwaddr[2] = mac_address[2];
	my_net_if.hwaddr[3] = mac_address[3];
	my_net_if.hwaddr[4] = mac_address[4];
	my_net_if.hwaddr[5] = mac_address[5];
  
    /* maximum transfer unit */
	my_net_if.mtu = 1500;
    
#if LWIP_NETIF_HOSTNAME
    /* Initialize interface hostname */
	my_net_if.hostname = "Ultimate-II";
#endif /* LWIP_NETIF_HOSTNAME */

    /*
     * Initialize the snmp variables and counters inside the struct netif.
     * The last argument should be replaced with your link speed, in units
     * of bits per second.
     */
    NETIF_INIT_SNMP(&my_net_if, snmp_ifType_ethernet_csmacd, 100000000);
  
    /* We directly use etharp_output() here to save a function call.
     * You can instead declare your own function an call etharp_output()
     * from it if you have to do some checks before sending (e.g. if link
     * is available...) */
    my_net_if.output = etharp_output;
    my_net_if.linkoutput = lwip_output_callback;

    /* device capabilities */
	my_net_if.flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_LINK_UP;

//	struct netif *nif = netif_add(&my_net_if, &my_ip, &my_netmask, &my_gateway,
//              this, lwip_init_callback, ethernet_input);
	struct netif *nif = netif_add(&my_net_if, &my_ip, &my_netmask, &my_gateway,
              this, lwip_init_callback, tcpip_input);

    /* device capabilities */
	my_net_if.flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_LINK_UP;

	//dump_hex(nif, sizeof(struct netif));

    if(nif) {
    	if (nif != &my_net_if) {
    		printf("** GOT OTHER POINTER **\n");
    	}
    	netif_set_default(nif);
    } else {
    	printf("Netif_add failed.\n");
    }
}

/*
__inline static bool check_time(DWORD timer, DWORD &variable, DWORD interval, char dbg)
{
    if (timer < variable) {
        if (((timer + 65536) - variable) > interval) {
            variable = timer;
            //printf("%c", dbg);
            return true;
        }
    } else if ((timer - variable) > interval) {
            variable = timer;
            //printf("%c", dbg);
            return true;
    }
    return false;
}

void NetworkLWIP :: lwip_poll()
{
    // these variables say when was the last event
    static DWORD dhcp_coarse = 0;
    static DWORD dhcp_fine = 0;
    static DWORD tcp_timer = 0;
    static DWORD dns_timer = 0;
    static DWORD arp_timer = 0;
    static DWORD ip_timer = 0;
    
    DWORD timer = (DWORD)ITU_MS_TIMER;

    if (check_time(timer, dhcp_coarse, DHCP_COARSE_TIMER_MSECS, 'C'))
        dhcp_coarse_tmr();
    if (check_time(timer, dhcp_fine, DHCP_FINE_TIMER_MSECS, 'F'))
        dhcp_fine_tmr();
    if (check_time(timer, tcp_timer, TCP_TMR_INTERVAL, 't'))
        tcp_tmr();
    if (check_time(timer, dns_timer, DNS_TMR_INTERVAL, 'd'))
        dns_tmr();
    if (check_time(timer, arp_timer, ARP_TMR_INTERVAL, 'a'))
        etharp_tmr();
    if (check_time(timer, ip_timer, IP_TMR_INTERVAL, 'i'))                
        ip_reass_tmr();

    // autoip_tmr();
    // igmp_tmr();
}
*/

bool NetworkLWIP :: input(BYTE *raw_buffer, BYTE *payload, int pkt_size)
{
	//dump_hex(payload, pkt_size);

	//struct pbuf_custom *pbuf = (struct pbuf_custom *)malloc(sizeof(struct pbuf_custom));
	struct pbuf_custom *pbuf = pbuf_fifo.pop();

	if (!pbuf) {
		printf("No memory");
		return false;
	}
	pbuf->custom_obj = this;
	pbuf->custom_free_function = lwip_free_callback;
	pbuf->buffer_start = raw_buffer;

	struct pbuf *p = &(pbuf->pbuf);
	p->flags |= PBUF_FLAG_IS_CUSTOM;
	p->len = p->tot_len = pkt_size;
	p->next = NULL;
	p->payload = payload;
	p->ref = 1;
	p->type = PBUF_REF;

/*
	struct pbuf *p = pbuf_alloc(PBUF_RAW, pkt_size + ETH_PAD_SIZE, PBUF_RAM);
	if (p == NULL) {
		printf("Help, no pbuf for incoming packet.");
		return false;
	}
	p->payload = ((BYTE *)p->payload) - 2;
	memcpy(p->payload, payload, pkt_size);
*/

	if (my_net_if.input(p, &my_net_if)!=ERR_OK) {
		LWIP_DEBUGF(NETIF_DEBUG, ("net_if_input: IP input error\n"));
		pbuf_free(p);
		return false;
	}
	return true;
}

void NetworkLWIP :: link_up()
{
    // Enable the network interface
    netif_set_up(&my_net_if);
	dhcp_start(&my_net_if);
}

void NetworkLWIP :: link_down()
{
	// Disable the network interface
	netif_set_link_down(&my_net_if);
	dhcp_stop(&my_net_if);
}

void NetworkLWIP :: set_mac_address(BYTE *mac)
{
	memcpy((void *)mac_address, (const void *)mac, 6);
}

void NetworkLWIP :: free_pbuf(struct pbuf_custom *pbuf)
{
	pbuf_fifo.push(pbuf);
}
