/*
 * Fuzz corosync's totem single-ring protocol (exec/totemsrp.c): the packet parser and the membership /
 * ordering state machine behind main_deliver_fn().
 *
 * totemsrp.c is #included so the harness can reach its static state machine. The network layer
 * (totemnet_*) is replaced by an in-memory loopback: everything the node sends is delivered back to
 * itself, which brings a one-node ring up (GATHER -> COMMIT -> RECOVERY -> OPERATIONAL) without sockets or
 * timers. The input is a sequence of [u16 length][packet] records injected into the live ring; after each
 * one the loopback drains a bounded number of replies. Every input builds and tears down a fresh instance.
 */
#include "../exec/totemsrp.c"

#define LOOPBACK_MAX 64
#define DRAIN_STEPS 48

struct stub_net {
	void *context;
	int (*deliver_fn)(void *context, const void *msg, unsigned int msg_len,
			  const struct sockaddr_storage *from);
	void (*target_set_completed)(void *context);
	int target_set_pending;
	unsigned int queued;
	unsigned int head;
	unsigned int sizes[LOOPBACK_MAX];
	unsigned char *pkts[LOOPBACK_MAX];
};

static struct stub_net stub_net;
static unsigned char stub_buffer[65536];

static void stub_log_printf(int level, int subsys, const char *function, const char *file, int line,
			    const char *format, ...)
{
	(void)level; (void)subsys; (void)function; (void)file; (void)line; (void)format;
}

static void stub_ring_id_load(struct memb_ring_id *ring_id, unsigned int nodeid)
{
	ring_id->rep = nodeid;
	ring_id->seq = 0x100;
}

static void stub_ring_id_store(const struct memb_ring_id *ring_id, unsigned int nodeid)
{
	(void)ring_id; (void)nodeid;
}

static void stub_enqueue(const void *msg, unsigned int len)
{
	unsigned int slot;

	if (stub_net.queued >= LOOPBACK_MAX) {
		return;
	}
	slot = (stub_net.head + stub_net.queued) % LOOPBACK_MAX;
	stub_net.pkts[slot] = malloc(len ? len : 1);
	if (stub_net.pkts[slot] == NULL) {
		abort();
	}
	memcpy(stub_net.pkts[slot], msg, len);
	stub_net.sizes[slot] = len;
	stub_net.queued++;
}

int totemnet_initialize(qb_loop_t *poll_handle, void **net_context, struct totem_config *totem_config,
			totemsrp_stats_t *stats, void *context,
			int (*deliver_fn)(void *, const void *, unsigned int, const struct sockaddr_storage *),
			int (*iface_change_fn)(void *, const struct totem_ip_address *, unsigned int),
			void (*mtu_changed)(void *, int), void (*target_set_completed)(void *))
{
	(void)poll_handle; (void)totem_config; (void)stats; (void)iface_change_fn; (void)mtu_changed;
	memset(&stub_net, 0, sizeof(stub_net));
	stub_net.context = context;
	stub_net.deliver_fn = deliver_fn;
	stub_net.target_set_completed = target_set_completed;
	*net_context = &stub_net;
	return 0;
}

void *totemnet_buffer_alloc(void *net_context) { (void)net_context; return stub_buffer; }
void totemnet_buffer_release(void *net_context, void *ptr) { (void)net_context; (void)ptr; }
int totemnet_processor_count_set(void *net_context, int processor_count) { (void)net_context; (void)processor_count; return 0; }
int totemnet_token_send(void *net_context, const void *msg, unsigned int msg_len) { (void)net_context; stub_enqueue(msg, msg_len); return 0; }
int totemnet_mcast_flush_send(void *net_context, const void *msg, unsigned int msg_len) { (void)net_context; stub_enqueue(msg, msg_len); return 0; }
int totemnet_mcast_noflush_send(void *net_context, const void *msg, unsigned int msg_len) { (void)net_context; stub_enqueue(msg, msg_len); return 0; }
int totemnet_recv_flush(void *net_context) { (void)net_context; return 0; }
int totemnet_send_flush(void *net_context) { (void)net_context; return 0; }
int totemnet_iface_set(void *net_context, const struct totem_ip_address *interface_addr, unsigned short ip_port, unsigned int iface_no) { (void)net_context; (void)interface_addr; (void)ip_port; (void)iface_no; return 0; }
int totemnet_iface_check(void *net_context) { (void)net_context; return 0; }
int totemnet_finalize(void *net_context) { (void)net_context; return 0; }
int totemnet_net_mtu_adjust(void *net_context, struct totem_config *totem_config) { (void)net_context; (void)totem_config; return 0; }
int totemnet_reconfigure(void *net_context, struct totem_config *totem_config) { (void)net_context; (void)totem_config; return 0; }
int totemnet_crypto_reconfigure_phase(void *net_context, struct totem_config *totem_config, cfg_message_crypto_reconfig_phase_t phase) { (void)net_context; (void)totem_config; (void)phase; return 0; }
void totemnet_stats_clear(void *net_context) { (void)net_context; }
const char *totemnet_iface_print(void *net_context) { (void)net_context; return "stub"; }
int totemnet_nodestatus_get(void *net_context, unsigned int nodeid, struct totem_node_status *node_status) { (void)net_context; (void)nodeid; (void)node_status; return 0; }
int totemnet_ifaces_get(void *net_context, char ***status, unsigned int *iface_count) { (void)net_context; (void)status; (void)iface_count; return 0; }
int totemnet_token_target_set(void *net_context, unsigned int target_nodeid) { (void)net_context; (void)target_nodeid; stub_net.target_set_pending = 1; return 0; }
int totemnet_crypto_set(void *net_context, const char *cipher_type, const char *hash_type) { (void)net_context; (void)cipher_type; (void)hash_type; return 0; }
int totemnet_recv_mcast_empty(void *net_context) { (void)net_context; return 0; }
int totemnet_member_add(void *net_context, const struct totem_ip_address *local, const struct totem_ip_address *member, int ring_no) { (void)net_context; (void)local; (void)member; (void)ring_no; return 0; }
int totemnet_member_remove(void *net_context, const struct totem_ip_address *member, int ring_no) { (void)net_context; (void)member; (void)ring_no; return 0; }

int totemconfig_commit_new_params(struct totem_config *totem_config, icmap_map_t map)
{
	(void)totem_config; (void)map;
	return 0;
}

static void stub_deliver(unsigned int nodeid, const void *msg, unsigned int msg_len, int endian_conversion_required)
{
	(void)nodeid; (void)msg; (void)msg_len; (void)endian_conversion_required;
}

static void stub_confchg(enum totem_configuration_type configuration_type,
			 const unsigned int *member_list, size_t member_list_entries,
			 const unsigned int *left_list, size_t left_list_entries,
			 const unsigned int *joined_list, size_t joined_list_entries,
			 const struct memb_ring_id *ring_id)
{
	(void)configuration_type; (void)member_list; (void)member_list_entries; (void)left_list;
	(void)left_list_entries; (void)joined_list; (void)joined_list_entries; (void)ring_id;
}

static void stub_waiting_trans_ack(int waiting_trans_ack) { (void)waiting_trans_ack; }

static struct sockaddr_storage from_addr;

static void stub_discard(void)
{
	while (stub_net.queued > 0) {
		free(stub_net.pkts[stub_net.head]);
		stub_net.head = (stub_net.head + 1) % LOOPBACK_MAX;
		stub_net.queued--;
	}
}

/*
 * The real network layer hands main_deliver_fn() a pointer into its maximum-size receive buffer, and some
 * handlers read a fixed-size tail (e.g. the ORF token's retransmit list) regardless of the received length.
 * Deliver every packet the same way, zero-padded, so those reads stay inside the buffer as they do in a
 * deployment.
 */
static unsigned char rx_buffer[65536];

static void deliver(const void *pkt, size_t len)
{
	if (len > sizeof(rx_buffer)) {
		len = sizeof(rx_buffer);
	}
	memset(rx_buffer, 0, sizeof(rx_buffer));
	memcpy(rx_buffer, pkt, len);
	main_deliver_fn(stub_net.context, rx_buffer, len, &from_addr);
}

static void drain(void)
{
	int step;

	for (step = 0; step < DRAIN_STEPS; step++) {
		if (stub_net.target_set_pending) {
			stub_net.target_set_pending = 0;
			stub_net.target_set_completed(stub_net.context);
			continue;
		}
		if (stub_net.queued == 0) {
			break;
		}
		unsigned int slot = stub_net.head;
		unsigned char *pkt = stub_net.pkts[slot];
		unsigned int len = stub_net.sizes[slot];

		stub_net.head = (stub_net.head + 1) % LOOPBACK_MAX;
		stub_net.queued--;
		deliver(pkt, len);
		free(pkt);
	}
	stub_discard();
}

int LLVMFuzzerInitialize(int *argc, char ***argv)
{
	struct sockaddr_in *sin = (struct sockaddr_in *)&from_addr;

	(void)argc; (void)argv;
	sin->sin_family = AF_INET;
	sin->sin_addr.s_addr = htonl(0x7f000001);
	return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	struct totem_config config;
	struct totemsrp_instance *instance;
	totempg_stats_t stats;
	void *srp_context = NULL;
	qb_loop_t *loop;
	size_t off = 0;

	memset(&config, 0, sizeof(config));
	memset(&stats, 0, sizeof(stats));
	config.interfaces = calloc(INTERFACE_MAX, sizeof(struct totem_interface));
	if (config.interfaces == NULL) {
		abort();
	}
	config.interfaces[0].configured = 1;
	config.interfaces[0].boundto.nodeid = 1;
	config.interfaces[0].boundto.family = AF_INET;
	config.interfaces[0].mcast_addr.family = AF_INET;
	config.interfaces[0].mcast_addr.nodeid = 1;
	config.totem_logging_configuration.log_printf = stub_log_printf;
	config.totem_memb_ring_id_create_or_load = stub_ring_id_load;
	config.totem_memb_ring_id_store = stub_ring_id_store;
	config.token_timeout = 1000;
	config.token_retransmit_timeout = 238;
	config.token_hold_timeout = 180;
	config.token_retransmits_before_loss_const = 4;
	config.token_warning = 75;
	config.join_timeout = 50;
	config.send_join_timeout = 0;
	config.consensus_timeout = 1200;
	config.merge_timeout = 200;
	config.downcheck_timeout = 1000;
	config.fail_to_recv_const = 2500;
	config.seqno_unchanged_const = 30;
	config.miss_count_const = 5;
	config.window_size = 50;
	config.max_messages = 17;
	config.max_network_delay = 50;
	config.heartbeat_failures_allowed = 0;
	config.net_mtu = 1400;
	config.threads = 0;

	loop = qb_loop_create();
	if (loop == NULL) {
		abort();
	}
	if (totemsrp_initialize(loop, &srp_context, &config, &stats, stub_deliver, stub_confchg,
				stub_waiting_trans_ack) != 0) {
		abort();
	}
	instance = srp_context;

	instance->iface_changes = 1;
	totemip_copy(&instance->my_addrs[0], &config.interfaces[0].boundto);
	instance->memb_ring_id_create_or_load(&instance->my_ring_id, instance->my_id.nodeid);
	instance->my_ring_id.seq++;
	instance->token_ring_id_seq = instance->my_ring_id.seq;
	memb_state_gather_enter(instance, TOTEMSRP_GSFROM_INTERFACE_CHANGE);
	drain();

	while (size - off >= 2) {
		size_t len = data[off] | ((size_t)data[off + 1] << 8);

		off += 2;
		if (len > size - off) {
			len = size - off;
		}
		deliver(data + off, len);
		off += len;
		drain();
	}

	totemsrp_finalize(instance);
	stub_discard();
	qb_loop_destroy(loop);
	free(config.interfaces);
	return 0;
}
