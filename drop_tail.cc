/*
 * drop_tail.cc
 *
 *  Created on: August 30, 2014
 *      Author: aousterh
 */

#include "drop_tail.h"
#include "api.h"
#include "api_impl.h"

#define DROP_TAIL_PORT_CAPACITY 128
#define DROP_TAIL_MAX_CAPACITY 256

DropTailQueueManager::DropTailQueueManager(PacketQueueBank *bank,
		uint32_t queue_capacity, Dropper &dropper)
	: m_bank(bank), m_q_capacity(queue_capacity), m_dropper(dropper)
{
	if (bank == NULL)
		throw std::runtime_error("bank should be non-NULL");
}

void DropTailQueueManager::enqueue(struct emu_packet *pkt,
		uint32_t port, uint32_t queue)
{
	if (m_bank->occupancy(port, queue) >= m_q_capacity) {
		/* no space to enqueue, drop this packet */
		adm_log_emu_router_dropped_packet(&g_state->stat);
		m_dropper.drop(pkt);
	} else {
		m_bank->enqueue(port, queue, pkt);
	}
}

DropTailRouter::DropTailRouter(uint16_t id, uint16_t port_capacity,
		Dropper &dropper)
	: m_bank(EMU_ROUTER_NUM_PORTS, 1, DROP_TAIL_MAX_CAPACITY),
	  m_cla(16, 0, EMU_ROUTER_NUM_PORTS, 0),
	  m_qm(&m_bank, port_capacity, dropper),
	  m_sch(&m_bank),
	  DropTailRouterBase(&m_cla, &m_qm, &m_sch, EMU_ROUTER_NUM_PORTS, id)
{}

DropTailRouter::~DropTailRouter() {}

DropTailEndpoint::DropTailEndpoint(uint16_t id, struct drop_tail_args *args,
		EmulationOutput &emu_output)
	: Endpoint(id), m_emu_output(emu_output)
{
	uint32_t port_capacity;

	/* use args if supplied, otherwise use defaults */
	if (args != NULL)
		port_capacity = args->port_capacity;
	else
		port_capacity = DROP_TAIL_PORT_CAPACITY;

	queue_create(&output_queue, port_capacity);
}

DropTailEndpoint::~DropTailEndpoint() {
	reset();
}

void DropTailEndpoint::reset() {
	struct emu_packet *packet;

	/* dequeue all queued packets */
	while (queue_dequeue(&output_queue, &packet) == 0)
		m_emu_output.free_packet(packet);
}

void DropTailEndpoint::new_packet(struct emu_packet *packet) {
	/* try to enqueue the packet */
	if (queue_enqueue(&output_queue, packet) != 0) {
		/* no space to enqueue, drop this packet */
		adm_log_emu_endpoint_dropped_packet(&g_state->stat);
		m_emu_output.drop(packet);
	}
}

void DropTailEndpoint::push(struct emu_packet *packet) {
	assert(packet->dst == id);

	/* pass the packet up the stack */
	m_emu_output.admit(packet);
}

void DropTailEndpoint::pull(struct emu_packet **packet) {
	/* dequeue one incoming packet if the queue is non-empty */
	*packet = NULL;
	queue_dequeue(&output_queue, packet);
}

DropTailEndpointGroup::DropTailEndpointGroup(uint16_t num_endpoints,
		uint16_t start_id, struct drop_tail_args *args)
	: EndpointGroup(num_endpoints) {
	CONSTRUCT_ENDPOINTS(start_id, num_endpoints, DropTailEndpoint, args);
}
