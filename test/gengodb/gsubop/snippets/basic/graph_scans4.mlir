module {
    func.func @main() {
    	%subop_result = subop.execution_group (){
            
            %g = gsubop.create_builtin_graph {builtin = standard} : !gsubop.graph<[vx : !gsubop.node_set<[vx_it : !gsubop.graph_set_iterator<["all"]>]>],[ex : !gsubop.edge_set<[ex_it : !gsubop.graph_set_iterator<["all"]>]>]>
            %g_scan = gsubop.scan_graph %g : !gsubop.graph<[vx : !gsubop.node_set<[vx_it : !gsubop.graph_set_iterator<["all"]>]>],[ex : !gsubop.edge_set<[ex_it : !gsubop.graph_set_iterator<["all"]>]>]> @nodes::@set({type = !gsubop.node_set<[vx_it : !gsubop.graph_set_iterator<["all"]>]>}), @edges::@set({type = !gsubop.edge_set<[ex_it : !gsubop.graph_set_iterator<["all"]>]>})
            %vx = subop.nested_map %g_scan [@nodes::@set] (%arg0, %arg1){
                %node_stream = gsubop.scan_node_set %arg1 : !gsubop.node_set<[vx_it : !gsubop.graph_set_iterator<["all"]>]> @nodes::@ref({type = !gsubop.node_ref<[node_id : i32],[incoming : !gsubop.edge_set<[incoming_it : !gsubop.graph_set_iterator<["incoming"]>]>],[outgoing : !gsubop.edge_set<[outgoing_it : !gsubop.graph_set_iterator<["outgoing"]>]>],[property : i64]>})
                tuples.return %node_stream : !tuples.tuplestream
            }
            %outgoing_sets = subop.gather %vx @nodes::@ref {outgoing => @outgoing::@set({type = !gsubop.edge_set<[outgoing_it : !gsubop.graph_set_iterator<["outgoing"]>]>})}

            %ex = subop.nested_map %outgoing_sets [@outgoing::@set] (%arg0, %arg1){
                %edge_stream = gsubop.scan_edge_set %arg1 : !gsubop.edge_set<[outgoing_it : !gsubop.graph_set_iterator<["outgoing"]>]> @edges::@ref({type = !gsubop.edge_ref<[edge_id : i32],[from : !gsubop.node_ref<[node_id1 : i32],[incoming1 : !gsubop.edge_set<[incoming_it1 : !gsubop.graph_set_iterator<["incoming"]>]>],[outgoing1 : !gsubop.edge_set<[outgoing_it1 : !gsubop.graph_set_iterator<["outgoing"]>]>],[property1 : i64]>],[to : !gsubop.node_ref<[node_id2 : i32],[incoming2 : !gsubop.edge_set<[incoming_it2 : !gsubop.graph_set_iterator<["incoming"]>]>],[outgoing2 : !gsubop.edge_set<[outgoing_it2 : !gsubop.graph_set_iterator<["outgoing"]>]>],[property2 : i64]>],[edge_prop : i64]>})
                tuples.return %edge_stream : !tuples.tuplestream
            }
            %resultEdges = subop.gather %ex @edges::@ref {edge_id => @edges::@id({type = i32}), to => @edges::@toRef({type = !gsubop.node_ref<[node_id2 : i32],[incoming2 : !gsubop.edge_set<[incoming_it2 : !gsubop.graph_set_iterator<["incoming"]>]>],[outgoing2 : !gsubop.edge_set<[outgoing_it2 : !gsubop.graph_set_iterator<["outgoing"]>]>],[property2 : i64]>})}
            %resultEdges0 = subop.gather %resultEdges @edges::@toRef {node_id2 => @nodes2::@id({type = i32})}

            %0 = subop.create !subop.result_table<[int32p0 : i32, int32p1 : i32]>
            subop.materialize %resultEdges0 {@edges::@id => int32p0, @nodes2::@id => int32p1}, %0 : !subop.result_table<[int32p0 : i32, int32p1 : i32]>
            %res = subop.create_from ["int32", "int32"] %0 : !subop.result_table<[int32p0 : i32, int32p1 : i32]> -> !subop.local_table<[int32p0 : i32, int32p1 : i32], ["int32", "int32"]>
            subop.execution_group_return %res : !subop.local_table<[int32p0 : i32, int32p1 : i32], ["int32", "int32"]>
        
        } -> !subop.local_table<[int32n0 : i32, int32n1 : i32], ["int32", "int32"]>
        subop.set_result 0 %subop_result : !subop.local_table<[int32n0 : i32, int32n1 : i32], ["int32", "int32"]>
        return
    }
}