#ifndef OPENOLT_STATS_COLLECTION_H_
#define OPENOLT_STATS_COLLECTION_H_

#include <openolt.grpc.pb.h>

extern "C"
{
#include <bal_model_types.h>
}

void init_stats();
void stop_collecting_statistics();
openolt::PortStatistics* get_default_port_statistics();
openolt::PortStatistics* collectPortStatistics(bcmbal_interface_key key);
#if 0
openolt::FlowStatistics* get_default_flow_statistics();
openolt::FlowStatistics* collectFlowStatistics(bcmbal_flow_id flow_id, bcmbal_flow_type flow_type);
void register_new_flow(bcmbal_flow_key key);
#endif


#endif