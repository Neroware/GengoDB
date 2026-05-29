module {
    func.func @main() {
    	%subop_result = subop.execution_group (){
            
            %graph = gsubop.create_builtin_graph {builtin = standard} : !gsubop.graph<[vx : !gsubop.node_set<[vx_it : !gsubop.graph_set_iterator<["all"]>]>],[ex : !gsubop.edge_set<[ex_it : !gsubop.graph_set_iterator<["all"]>]>]>
            %graph_scan = gsubop.scan_graph %graph : !gsubop.graph<[vx : !gsubop.node_set<[vx_it : !gsubop.graph_set_iterator<["all"]>]>],[ex : !gsubop.edge_set<[ex_it : !gsubop.graph_set_iterator<["all"]>]>]> @nodes::@set({type = !gsubop.node_set<[vx_it : !gsubop.graph_set_iterator<["all"]>]>}), @edges::@set({type = !gsubop.edge_set<[ex_it : !gsubop.graph_set_iterator<["all"]>]>})
            %node_refs = subop.nested_map %graph_scan [@nodes::@set] (%arg0, %arg1){
                %node_stream = gsubop.scan_node_set %arg1 : !gsubop.node_set<[vx_it : !gsubop.graph_set_iterator<["all"]>]> @nodes::@ref({type = !gsubop.node_ref<[node_id : i64],[incoming : !gsubop.edge_set<[incoming_it : !gsubop.graph_set_iterator<["incoming"]>]>],[outgoing : !gsubop.edge_set<[outgoing_it : !gsubop.graph_set_iterator<["outgoing"]>]>],[property : i64]>})
                tuples.return %node_stream : !tuples.tuplestream
            }
            %outgoing_sets = subop.gather %node_refs @nodes::@ref {outgoing => @nodes::@outgoing({type = !gsubop.edge_set<[outgoing_it : !gsubop.graph_set_iterator<["outgoing"]>]>})}
            %edge_refs = subop.nested_map %outgoing_sets [@nodes::@outgoing] (%arg0, %arg1){
                %edge_stream = gsubop.scan_edge_set %arg1 : !gsubop.edge_set<[outgoing_it : !gsubop.graph_set_iterator<["outgoing"]>]> @edges::@ref({type = !gsubop.edge_ref<[edge_id : i64],[from : !gsubop.node_ref<[node_id1 : i64],[incoming1 : !gsubop.edge_set<[incoming_it1 : !gsubop.graph_set_iterator<["incoming"]>]>],[outgoing1 : !gsubop.edge_set<[outgoing_it1 : !gsubop.graph_set_iterator<["outgoing"]>]>],[property1 : i64]>],[to : !gsubop.node_ref<[node_id2 : i64],[incoming2 : !gsubop.edge_set<[incoming_it2 : !gsubop.graph_set_iterator<["incoming"]>]>],[outgoing2 : !gsubop.edge_set<[outgoing_it2 : !gsubop.graph_set_iterator<["outgoing"]>]>],[property2 : i64]>],[edge_prop : i64]>})
                tuples.return %edge_stream : !tuples.tuplestream
            }
            subop.reduce %edge_refs @nodes::@ref [] ["property"] ([],[%property]) {
                %c1 = arith.constant 1 : i64
                %newProp = arith.addi %property, %c1 : i64
                tuples.return %newProp : i64
            }

            %node_refs0 = subop.nested_map %graph_scan [@nodes::@set] (%arg0, %arg1){
                %node_stream = gsubop.scan_node_set %arg1 : !gsubop.node_set<[vx_it : !gsubop.graph_set_iterator<["all"]>]> @nodes::@ref({type = !gsubop.node_ref<[node_id : i64],[incoming : !gsubop.edge_set<[incoming_it : !gsubop.graph_set_iterator<["incoming"]>]>],[outgoing : !gsubop.edge_set<[outgoing_it : !gsubop.graph_set_iterator<["outgoing"]>]>],[property : i64]>})
                tuples.return %node_stream : !tuples.tuplestream
            }
            %node_props = subop.gather %node_refs0 @nodes::@ref {property => @nodes::@prop({type = i64})}
            %node_ids = subop.gather %node_props @nodes::@ref {node_id => @nodes::@id({type = i64})}

            %0 = subop.create !subop.result_table<[int64p0 : i64, int64p1 : i64]>
            subop.materialize %node_ids {@nodes::@id => int64p0, @nodes::@prop => int64p1}, %0 : !subop.result_table<[int64p0 : i64, int64p1 : i64]>
            %res = subop.create_from ["nid", "prop"] %0 : !subop.result_table<[int64p0 : i64, int64p1 : i64]> -> !subop.local_table<[int64p0 : i64, int64p1 : i64], ["nid", "prop"]>
            subop.execution_group_return %res : !subop.local_table<[int64p0 : i64, int64p1 : i64], ["nid", "prop"]>
        
        } -> !subop.local_table<[int64n0 : i64, int64n1 : i64], ["nid", "prop"]>
        subop.set_result 0 %subop_result : !subop.local_table<[int64n0 : i64, int64n1 : i64], ["nid", "prop"]>
        return
    }
}
