/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2017 Intel Corporation
 */

#include <fcntl.h>
#include <pthread.h>
#include <unistd.h>

#include <rte_string_fns.h>
#include <ethdev_driver.h>
#include <ethdev_vdev.h>
// #include <rte_kni.h>
#include <rte_kvargs.h>
#include <rte_malloc.h>
#include <rte_bus_vdev.h>

#define NKERN 14
#define XRTA_MAX_QUEUE_PER_PORT (NKERN*3)

// #define MAX_KNI_PORTS 8

// --- KNI_ETHER_MTU not used, but need something like this I think
#define KNI_ETHER_MTU(mbuf_size)       \
	((mbuf_size) - RTE_ETHER_HDR_LEN) /**< Ethernet MTU. */

#define ETH_XRTA_NO_REQUEST_THREAD_ARG	"no_request_thread"
static const char * const valid_arguments[] = {
	ETH_XRTA_NO_REQUEST_THREAD_ARG,
	NULL
};

// This driver originated as a copy of /drivers/net/kni driver from DPDK 21.11LTS.  
// Each port corresponds to one U50 card.   Each U50 card contains NKERN VITIS processing kernels.
// Each kernel has one tx queue and two rx queues.  Packets on the tx queue contain configuration 
// for specifying the kernel run.  Each packet received from the appliation on the tx queue causes
// a sequence of raw data packets (currently set to 1024 packets) on the outd rx-queue and one
// trigger hit packet on the outt rx-queue.  If the tx-packet has a configuration error, it is
// returned on the outd rx-queue and no kernel run is initiated.
//
// One tx packet attempts to simulate the reception of the raw data and trigger primitive packets
// from 8ms from one link (10 links per APA) with 256 channels and 14 bit ADC values on a 2MHz 
// clock. A packet with 16 samples (8us) fits into 7168 bytes which is a nice size for a jumbo 
// packet.  Each kernel invocation handles 1024 packets which is 8ms, just over 2 drift times. 
// It also generates one trigger packet (for now, could be more). 
//
// I suspect the trick Xilinx use to make the opencl calls work efficiently is to run a large 
// number of packets through with one invocation of the opencl synchronisation, which works fine.  
// Perhaps later by using host buffers we may be able to remove this limitation and allow a 
// larger number of small kernel runs.  Not for now.
//
// This part explains how the polling works.  OpenCL provides a callback when a kernel completes:
//  -- If there is another tx packet waiting, the next kernel job should be started immediately 
//     in a call to run() and then the packets from the just-finished kernel job returned to the 
//     application. If there is another tx_packet waiting, the next sg table can be prepared
//     at this point so it is in parallel with the kernel running.  The WaitForData variable 
//     should be zero when the kernel completes and is kept as zero.
//  -- If there is no tx packet waiting when a kernel finishes, the flag WaitForData=1 is set 
//     and the packets from the current kernel job returned to the application.  If there are
//     sufficient unused rx mbufs to prepare the descriptor table (all but the tx) then this
//     is done at this point.
//  -- When there is nothing waiting but something could arrive, the application will be calling
//     both the rx_burst and tx_burst routines, these should check for both a new arrival and
//     for the ability to make a new descriptor table.
// The conclusion is that there is a WaitForData flag, and a flag to say whether the next
// rx_ descriptor tables have been prepared. There are also two routines that get called from
// various places, run() and prepare_descriptors().  The run() routine is similar to in host_hm.
//

struct eth_xrta_args {
	int no_request_thread;
};

struct pmd_queue_stats {
	uint64_t pkts;
	uint64_t bytes;
};

struct pmd_queue {
	struct pmd_internals *internals;
	struct rte_mempool *mb_pool;

	struct pmd_queue_stats rx;
	struct pmd_queue_stats tx;
};

struct pmd_internals {
	struct rte_xrta *xrta;
	uint16_t port_id;
	int is_xrta_started;

	pthread_t thread;
	int stop_thread;
	int no_request_thread;

	struct rte_ether_addr eth_addr;

	struct pmd_queue rx_queues[XRTA_MAX_QUEUE_PER_PORT];
	struct pmd_queue tx_queues[XRTA_MAX_QUEUE_PER_PORT];
};

static const struct rte_eth_link pmd_link = {
		.link_speed = RTE_ETH_SPEED_NUM_10G,
		.link_duplex = RTE_ETH_LINK_FULL_DUPLEX,
		.link_status = RTE_ETH_LINK_DOWN,
		.link_autoneg = RTE_ETH_LINK_FIXED,
};

static int is_xrta_initialized;   // Gets incremented and decremented in different threads

RTE_LOG_REGISTER_DEFAULT(eth_xrta_logtype, NOTICE);

#define PMD_LOG(level, fmt, args...) \
	rte_log(RTE_LOG_ ## level, eth_xrta_logtype, \
		"%s(): " fmt "\n", __func__, ##args)

static uint16_t
eth_xrta_rx(void *q, struct rte_mbuf **bufs, uint16_t nb_bufs)
{
	struct pmd_queue *kni_q = q;
	// struct rte_xrta *kni = kni_q->internals->xrta;
	uint16_t nb_pkts=0;
	// int i;

        // Copied from null
        if ((q == NULL) || (bufs == NULL) || (nb_bufs == 0))
                return 0;

	// nb_pkts = rte_kni_rx_burst(kni, bufs, nb_bufs);
	// for (i = 0; i < nb_pkts; i++)
	//	bufs[i]->port = kni_q->internals->port_id;

	kni_q->rx.pkts += nb_pkts;

	return nb_pkts;
}

static uint16_t
eth_xrta_tx(void *q, struct rte_mbuf **bufs, uint16_t nb_bufs)
{
	struct pmd_queue *kni_q = q;
	// struct rte_xrta *kni = kni_q->internals->xrta;
	uint16_t nb_pkts = 0;
        int i;

	//gb nb_pkts =  rte_kni_tx_burst(kni, bufs, nb_bufs);

	// Copied from null
        if ((q == NULL) || (bufs == NULL))
                return 0;

	// Copied from null
        for (i = 0; i < nb_bufs; i++)
                rte_pktmbuf_free(bufs[i]);

        // Useful copied from null:  rte_atomic64_add(&(h->tx_pkts), i);

	kni_q->tx.pkts += nb_pkts;

	return nb_pkts;
}

#if 0
static void *
kni_handle_request(void *param)
{
	struct pmd_internals *internals = param;
#define MS 1000

	while (!internals->stop_thread) {
		rte_kni_handle_request(internals->xrta);
		usleep(500 * MS);
	}

	return param;
}

static int
eth_kni_start(struct rte_eth_dev *dev)
{
	struct pmd_internals *internals = dev->data->dev_private;
	uint16_t port_id = dev->data->port_id;
	struct rte_mempool *mb_pool;
	struct rte_kni_conf conf = {{0}};
	const char *name = dev->device->name + 4; /* remove net_ */

	mb_pool = internals->rx_queues[0].mb_pool;
	strlcpy(conf.name, name, RTE_KNI_NAMESIZE);
	conf.force_bind = 0;
	conf.group_id = port_id;
	conf.mbuf_size =
		rte_pktmbuf_data_room_size(mb_pool) - RTE_PKTMBUF_HEADROOM;
	conf.mtu = KNI_ETHER_MTU(conf.mbuf_size);

	internals->xrta = rte_kni_alloc(mb_pool, &conf, NULL);
	if (internals->xrta == NULL) {
		PMD_LOG(ERR,
			"Fail to create kni interface for port: %d",
			port_id);
		return -1;
	}

	return 0;
}
#endif

// dev_ops: dev_start
static int
eth_xrta_dev_start(struct rte_eth_dev *dev)
{
	struct pmd_internals *internals = dev->data->dev_private;
	int ret = 0;

	if (internals->is_xrta_started == 0) {
		if (ret)
			return -1;
		internals->is_xrta_started = 1;
	}

        // --- kni at this point started a thread, just above, in the if statement, it
	// --- called eth_kni_start() which is just above which also set up the 
	// --- communication with the kni part of DPDK.

	dev->data->dev_link.link_status = 1;

	return 0;
}

// I think dev_stop gets called on PRIMARY and SECONDARY but dev_close only on PRIMARY 

// dev_ops: dev_stop
static int
eth_xrta_dev_stop(struct rte_eth_dev *dev)
{
	// struct pmd_internals *internals = dev->data->dev_private;
	// int ret;

        // --- kni at this point stopped a thread that talks with the kernel
	// --- and adjusted some variables in the intrnals area

	dev->data->dev_link.link_status = 0;
	dev->data->dev_started = 0;

	return 0;
}

// dev_ops: dev_close
static int
eth_xrta_close(struct rte_eth_dev *eth_dev)
{
	// struct pmd_internals *internals = eth_dev->data->dev_private;
	int ret = 0;

	if (rte_eal_process_type() != RTE_PROC_PRIMARY)
		return 0;

	ret = eth_xrta_dev_stop(eth_dev);
	if (ret)
		PMD_LOG(WARNING, "Not able to stop xrta for %s",
			eth_dev->data->name);

	/* mac_addrs must not be freed alone because part of dev_private */
	eth_dev->data->mac_addrs = NULL;

	// --- kni at this point did something towards the linux network stack.
	// --- ret = rte_kni_release(internals->xrta);

	return ret;
}

// dev_ops: dev_configure
static int
eth_xrta_dev_configure(struct rte_eth_dev *dev __rte_unused)
{
	return 0;
}

// dev_ops: dev_infos_get
static int
eth_xrta_dev_info(struct rte_eth_dev *dev __rte_unused,
		struct rte_eth_dev_info *dev_info)
{
	dev_info->max_mac_addrs = 1;
	dev_info->max_rx_pktlen = UINT32_MAX;
	dev_info->max_rx_queues = XRTA_MAX_QUEUE_PER_PORT;
	dev_info->max_tx_queues = XRTA_MAX_QUEUE_PER_PORT;
	dev_info->min_rx_bufsize = 0;

	return 0;
}

// dev_ops: rx_queue_setup
static int
eth_xrta_rx_queue_setup(struct rte_eth_dev *dev,
		uint16_t rx_queue_id,
		uint16_t nb_rx_desc __rte_unused,
		unsigned int socket_id __rte_unused,
		const struct rte_eth_rxconf *rx_conf __rte_unused,
		struct rte_mempool *mb_pool)
{
	struct pmd_internals *internals = dev->data->dev_private;
	struct pmd_queue *q;

	q = &internals->rx_queues[rx_queue_id];
	q->internals = internals;
	q->mb_pool = mb_pool;

	dev->data->rx_queues[rx_queue_id] = q;

	return 0;
}

// dev_ops: tx_queue_setup
static int
eth_xrta_tx_queue_setup(struct rte_eth_dev *dev,
		uint16_t tx_queue_id,
		uint16_t nb_tx_desc __rte_unused,
		unsigned int socket_id __rte_unused,
		const struct rte_eth_txconf *tx_conf __rte_unused)
{
	struct pmd_internals *internals = dev->data->dev_private;
	struct pmd_queue *q;

	q = &internals->tx_queues[tx_queue_id];
	q->internals = internals;

	dev->data->tx_queues[tx_queue_id] = q;

	return 0;
}

// dev_ops: link_update
static int
eth_xrta_link_update(struct rte_eth_dev *dev __rte_unused,
		int wait_to_complete __rte_unused)
{
	return 0;
}

// dev_ops: stats_get
static int
eth_xrta_stats_get(struct rte_eth_dev *dev, struct rte_eth_stats *stats)
{
	unsigned long rx_packets_total = 0, rx_bytes_total = 0;
	unsigned long tx_packets_total = 0, tx_bytes_total = 0;
	struct rte_eth_dev_data *data = dev->data;
	unsigned int i, num_stats;
	struct pmd_queue *q;

	num_stats = RTE_MIN((unsigned int)RTE_ETHDEV_QUEUE_STAT_CNTRS,
			data->nb_rx_queues);
	for (i = 0; i < num_stats; i++) {
		q = data->rx_queues[i];
		stats->q_ipackets[i] = q->rx.pkts;
		stats->q_ibytes[i] = q->rx.bytes;
		rx_packets_total += stats->q_ipackets[i];
		rx_bytes_total += stats->q_ibytes[i];
	}

	num_stats = RTE_MIN((unsigned int)RTE_ETHDEV_QUEUE_STAT_CNTRS,
			data->nb_tx_queues);
	for (i = 0; i < num_stats; i++) {
		q = data->tx_queues[i];
		stats->q_opackets[i] = q->tx.pkts;
		stats->q_obytes[i] = q->tx.bytes;
		tx_packets_total += stats->q_opackets[i];
		tx_bytes_total += stats->q_obytes[i];
	}

	stats->ipackets = rx_packets_total;
	stats->ibytes = rx_bytes_total;
	stats->opackets = tx_packets_total;
	stats->obytes = tx_bytes_total;

	return 0;
}

// dev_ops: stats_reset
static int
eth_xrta_stats_reset(struct rte_eth_dev *dev)
{
	struct rte_eth_dev_data *data = dev->data;
	struct pmd_queue *q;
	unsigned int i;

	for (i = 0; i < data->nb_rx_queues; i++) {
		q = data->rx_queues[i];
		q->rx.pkts = 0;
		q->rx.bytes = 0;
	}
	for (i = 0; i < data->nb_tx_queues; i++) {
		q = data->tx_queues[i];
		q->tx.pkts = 0;
		q->tx.bytes = 0;
	}

	return 0;
}

static const struct eth_dev_ops eth_xrta_ops = {
	.dev_start      = eth_xrta_dev_start,
	.dev_stop       = eth_xrta_dev_stop,
	.dev_close      = eth_xrta_close,             // dev_close calls dev_stop
	.dev_configure  = eth_xrta_dev_configure,
	.dev_infos_get  = eth_xrta_dev_info,
	.rx_queue_setup = eth_xrta_rx_queue_setup,
	.tx_queue_setup = eth_xrta_tx_queue_setup,
	.link_update    = eth_xrta_link_update,
	.stats_get      = eth_xrta_stats_get,
	.stats_reset    = eth_xrta_stats_reset,
};

// Called from probe()
static struct rte_eth_dev *
eth_xrta_create(struct rte_vdev_device *vdev,
		struct eth_xrta_args *args,
		unsigned int numa_node)
{
	struct pmd_internals *internals;
	struct rte_eth_dev_data *data;
	struct rte_eth_dev *eth_dev;

	PMD_LOG(INFO, "Creating xrta ethdev on numa socket %u",
			numa_node);

	/* reserve an ethdev entry */
	eth_dev = rte_eth_vdev_allocate(vdev, sizeof(*internals));
	if (!eth_dev)
		return NULL;

	internals = eth_dev->data->dev_private;
	internals->port_id = eth_dev->data->port_id;
	data = eth_dev->data;
	data->nb_rx_queues = XRTA_MAX_QUEUE_PER_PORT;
	data->nb_tx_queues = XRTA_MAX_QUEUE_PER_PORT;
	data->dev_link = pmd_link;
	data->mac_addrs = &internals->eth_addr;
	data->promiscuous = 1;
	data->all_multicast = 1;
	data->dev_flags |= RTE_ETH_DEV_AUTOFILL_QUEUE_XSTATS;

	rte_eth_random_addr(internals->eth_addr.addr_bytes);

	eth_dev->dev_ops = &eth_xrta_ops;

	internals->no_request_thread = args->no_request_thread;

	return eth_dev;
}

// Called from probe()
static int
xrta_init(void)
{
	int ret = 0;

	if (is_xrta_initialized == 0) {
		// ret = rte_kni_init(MAX_KNI_PORTS);
		if (ret < 0)
			return ret;
	}

	is_xrta_initialized++;

	return 0;
}

// Called from probe()
static int
eth_xrta_kvargs_process(struct eth_xrta_args *args, const char *params)
{
	struct rte_kvargs *kvlist;

	kvlist = rte_kvargs_parse(params, valid_arguments);
	if (kvlist == NULL)
		return -1;

	memset(args, 0, sizeof(struct eth_xrta_args));

	if (rte_kvargs_count(kvlist, ETH_XRTA_NO_REQUEST_THREAD_ARG) == 1)
		args->no_request_thread = 1;

	rte_kvargs_free(kvlist);

	return 0;
}

// rte_vdev_driver: probe
static int
eth_xrta_probe(struct rte_vdev_device *vdev)
{
	struct rte_eth_dev *eth_dev;
	struct eth_xrta_args args;
	const char *name;
	const char *params;
	int ret;

	name = rte_vdev_device_name(vdev);
	params = rte_vdev_device_args(vdev);
	PMD_LOG(INFO, "Initializing eth_kni for %s", name);

	if (rte_eal_process_type() == RTE_PROC_SECONDARY) {
		eth_dev = rte_eth_dev_attach_secondary(name);
		if (!eth_dev) {
			PMD_LOG(ERR, "Failed to probe %s", name);
			return -1;
		}
		/* TODO: request info from primary to set up Rx and Tx */
		eth_dev->dev_ops = &eth_xrta_ops;
		eth_dev->device = &vdev->device;
		rte_eth_dev_probing_finish(eth_dev);
		return 0;
	}

	// Function defined just above
	ret = eth_xrta_kvargs_process(&args, params);
	if (ret < 0)
		return ret;

        // Function defined just above
	ret = xrta_init();
	if (ret < 0)
		return ret;

	// Function defined just above
	eth_dev = eth_xrta_create(vdev, &args, rte_socket_id());
	if (eth_dev == NULL)
		goto xrta_uninit;

	eth_dev->rx_pkt_burst = eth_xrta_rx;    // Post the pointers to the rx and tx functions
	eth_dev->tx_pkt_burst = eth_xrta_tx;

	rte_eth_dev_probing_finish(eth_dev);
	return 0;

xrta_uninit:
	is_xrta_initialized--;
	if (is_xrta_initialized == 0) {
		// rte_kni_close();
	}
	return -1;
}

// rte_vdev_driver: remove
static int
eth_xrta_remove(struct rte_vdev_device *vdev)
{
	struct rte_eth_dev *eth_dev;
	const char *name;
	int ret;

	name = rte_vdev_device_name(vdev);
	PMD_LOG(INFO, "Un-Initializing eth_kni for %s", name);

	/* find the ethdev entry */
	eth_dev = rte_eth_dev_allocated(name);
	if (eth_dev != NULL) {
		if (rte_eal_process_type() != RTE_PROC_PRIMARY) {
			ret = eth_xrta_dev_stop(eth_dev);
			if (ret != 0)
				return ret;
			return rte_eth_dev_release_port(eth_dev);
		}
		eth_xrta_close(eth_dev);
		rte_eth_dev_release_port(eth_dev);
	}

	is_xrta_initialized--;
	if (is_xrta_initialized == 0) {
		// rte_kni_close();
	}

	return 0;
}

static struct rte_vdev_driver eth_xrta_drv = {
	.probe = eth_xrta_probe,
	.remove = eth_xrta_remove,
};

RTE_PMD_REGISTER_VDEV(net_xrta, eth_xrta_drv);
RTE_PMD_REGISTER_PARAM_STRING(net_xrta, ETH_XRTA_NO_REQUEST_THREAD_ARG "=<int>");
