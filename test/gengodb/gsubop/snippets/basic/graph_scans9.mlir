module {
    func.func @main() {
    	%subop_result = subop.execution_group (){
            
            %g = gsubop.create_builtin_graph {builtin = standard} : !gsubop.graph<[vx : !gsubop.node_set<[vx_it : !gsubop.graph_set_iterator<["all"]>]>],[ex : !gsubop.edge_set<[ex_it : !gsubop.graph_set_iterator<["all"]>]>]>
            
            // Stream A
            %streamA0 = gsubop.scan_graph %g : !gsubop.graph<[vx : !gsubop.node_set<[vx_it : !gsubop.graph_set_iterator<["all"]>]>],[ex : !gsubop.edge_set<[ex_it : !gsubop.graph_set_iterator<["all"]>]>]> @nodes::@set({type = !gsubop.node_set<[vx_it : !gsubop.graph_set_iterator<["all"]>]>}), @edges::@set({type = !gsubop.edge_set<[ex_it : !gsubop.graph_set_iterator<["all"]>]>})
            %streamA1 = subop.nested_map %streamA0 [@nodes::@set] (%arg0, %arg1){
                %node_stream = gsubop.scan_node_set %arg1 : !gsubop.node_set<[vx_it : !gsubop.graph_set_iterator<["all"]>]> @nodes::@ref({type = !gsubop.node_ref<[node_id : i32],[incoming : !gsubop.edge_set<[incoming_it : !gsubop.graph_set_iterator<["incoming"]>]>],[outgoing : !gsubop.edge_set<[outgoing_it : !gsubop.graph_set_iterator<["outgoing"]>]>],[property : i64]>})
                tuples.return %node_stream : !tuples.tuplestream
            }
            %streamA2 = subop.gather %streamA1 @nodes::@ref {outgoing => @outgoing::@set({type = !gsubop.edge_set<[outgoing_it : !gsubop.graph_set_iterator<["outgoing"]>]>})}

            %streamA3 = subop.nested_map %streamA2 [@outgoing::@set] (%arg0, %arg1){
                %edge_stream = gsubop.scan_edge_set %arg1 : !gsubop.edge_set<[outgoing_it : !gsubop.graph_set_iterator<["outgoing"]>]> @edges::@ref({type = !gsubop.edge_ref<[edge_id : i32],[from : !gsubop.node_ref<[node_id1 : i32],[incoming1 : !gsubop.edge_set<[incoming_it1 : !gsubop.graph_set_iterator<["incoming"]>]>],[outgoing1 : !gsubop.edge_set<[outgoing_it1 : !gsubop.graph_set_iterator<["outgoing"]>]>],[property1 : i64]>],[to : !gsubop.node_ref<[node_id2 : i32],[incoming2 : !gsubop.edge_set<[incoming_it2 : !gsubop.graph_set_iterator<["incoming"]>]>],[outgoing2 : !gsubop.edge_set<[outgoing_it2 : !gsubop.graph_set_iterator<["outgoing"]>]>],[property2 : i64]>],[edge_prop : i64]>})
                tuples.return %edge_stream : !tuples.tuplestream
            }
            %streamA4 = subop.gather %streamA3 @edges::@ref {edge_id => @edges::@id({type = i32}), to => @edges::@toRef({type = !gsubop.node_ref<[node_id2 : i32],[incoming2 : !gsubop.edge_set<[incoming_it2 : !gsubop.graph_set_iterator<["incoming"]>]>],[outgoing2 : !gsubop.edge_set<[outgoing_it2 : !gsubop.graph_set_iterator<["outgoing"]>]>],[property2 : i64]>})}
            %streamA5 = subop.gather %streamA4 @edges::@toRef {node_id2 => @nodes::@id({type = i32})}
            subop.scatter %streamA5 @edges::@toRef { @edges::@id => property2 }

            // Stream B
            %streamB0 = gsubop.scan_graph %g : !gsubop.graph<[vx : !gsubop.node_set<[vx_it : !gsubop.graph_set_iterator<["all"]>]>],[ex : !gsubop.edge_set<[ex_it : !gsubop.graph_set_iterator<["all"]>]>]> @nodes::@set({type = !gsubop.node_set<[vx_it : !gsubop.graph_set_iterator<["all"]>]>}), @edges::@set({type = !gsubop.edge_set<[ex_it : !gsubop.graph_set_iterator<["all"]>]>})
            %streamB1 = subop.nested_map %streamB0 [@nodes::@set] (%arg0, %arg1){
                %node_stream = gsubop.scan_node_set %arg1 : !gsubop.node_set<[vx_it : !gsubop.graph_set_iterator<["all"]>]> @nodes::@ref({type = !gsubop.node_ref<[node_id : i32],[incoming : !gsubop.edge_set<[incoming_it : !gsubop.graph_set_iterator<["incoming"]>]>],[outgoing : !gsubop.edge_set<[outgoing_it : !gsubop.graph_set_iterator<["outgoing"]>]>],[property : i64]>})
                tuples.return %node_stream : !tuples.tuplestream
            }
            %streamB2 = subop.gather %streamB1 @nodes::@ref {outgoing => @outgoing::@set({type = !gsubop.edge_set<[outgoing_it : !gsubop.graph_set_iterator<["outgoing"]>]>})}

            %streamB3 = subop.nested_map %streamB2 [@outgoing::@set] (%arg0, %arg1){
                %edge_stream = gsubop.scan_edge_set %arg1 : !gsubop.edge_set<[outgoing_it : !gsubop.graph_set_iterator<["outgoing"]>]> @edges::@ref({type = !gsubop.edge_ref<[edge_id : i32],[from : !gsubop.node_ref<[node_id1 : i32],[incoming1 : !gsubop.edge_set<[incoming_it1 : !gsubop.graph_set_iterator<["incoming"]>]>],[outgoing1 : !gsubop.edge_set<[outgoing_it1 : !gsubop.graph_set_iterator<["outgoing"]>]>],[property1 : i64]>],[to : !gsubop.node_ref<[node_id2 : i32],[incoming2 : !gsubop.edge_set<[incoming_it2 : !gsubop.graph_set_iterator<["incoming"]>]>],[outgoing2 : !gsubop.edge_set<[outgoing_it2 : !gsubop.graph_set_iterator<["outgoing"]>]>],[property2 : i64]>],[edge_prop : i64]>})
                tuples.return %edge_stream : !tuples.tuplestream
            }
            %streamB4 = subop.gather %streamB3 @edges::@ref {edge_id => @edges::@id({type = i32}), to => @edges::@toRef({type = !gsubop.node_ref<[node_id2 : i32],[incoming2 : !gsubop.edge_set<[incoming_it2 : !gsubop.graph_set_iterator<["incoming"]>]>],[outgoing2 : !gsubop.edge_set<[outgoing_it2 : !gsubop.graph_set_iterator<["outgoing"]>]>],[property2 : i64]>}), edge_prop => @edges::@property({type = i64})}
            %streamB5 = subop.gather %streamB4 @edges::@toRef {node_id2 => @nodes::@id({type = i32}), property2 => @nodes::@property({type = i64})}

            %streamB6 = gsubop.node_count %streamB5, %g -> @graph::@numVertices({type = index}) : !gsubop.graph<[vx : !gsubop.node_set<[vx_it : !gsubop.graph_set_iterator<["all"]>]>],[ex : !gsubop.edge_set<[ex_it : !gsubop.graph_set_iterator<["all"]>]>]>

            %0 = subop.create !subop.result_table<[eid_i32 : i32, eprop_i64 : i64, nid_i32 : i32, nprop_i64 : i64, ncount_index : index]>
            subop.materialize %streamB6 {@edges::@id => eid_i32, @edges::@property => eprop_i64, @nodes::@id => nid_i32, @nodes::@property => nprop_i64, @graph::@numVertices => ncount_index}, %0 : !subop.result_table<[eid_i32 : i32, eprop_i64 : i64, nid_i32 : i32, nprop_i64 : i64, ncount_index : index]>
            %res = subop.create_from ["eid", "eprop", "nid", "nprop", "ncount"] %0 : !subop.result_table<[eid_i32 : i32, eprop_i64 : i64, nid_i32 : i32, nprop_i64 : i64, ncount_index : index]> -> !subop.local_table<[eid_i32 : i32, eprop_i64 : i64, nid_i32 : i32, nprop_i64 : i64, ncount_index : index], ["eid", "eprop", "nid", "nprop", "ncount"]>
            subop.execution_group_return %res : !subop.local_table<[eid_i32 : i32, eprop_i64 : i64, nid_i32 : i32, nprop_i64 : i64, ncount_index : index], ["eid", "eprop", "nid", "nprop", "ncount"]>
        
        } -> !subop.local_table<[eid_i32n : i32, eprop_i64n : i64, nid_i32n : i32, nprop_i64n : i64, ncount_indexn : index], ["eid", "eprop", "nid", "nprop", "ncount"]>
        subop.set_result 0 %subop_result : !subop.local_table<[eid_i32n : i32, eprop_i64n : i64, nid_i32n : i32, nprop_i64n : i64, ncount_indexn : index], ["eid", "eprop", "nid", "nprop", "ncount"]>
        return
    }
}