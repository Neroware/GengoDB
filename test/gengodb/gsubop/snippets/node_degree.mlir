module {
    func.func @main() {
    	%subop_result = subop.execution_group (){
            
            %g = gsubop.create_builtin_graph {builtin = standard} : !gsubop.graph<[vx : !gsubop.node_set<[vx_it : !gsubop.graph_set_iterator<["all"]>]>],[ex : !gsubop.edge_set<[ex_it : !gsubop.graph_set_iterator<["all"]>]>]>
            %g_scan = gsubop.scan_graph %g : !gsubop.graph<[vx : !gsubop.node_set<[vx_it : !gsubop.graph_set_iterator<["all"]>]>],[ex : !gsubop.edge_set<[ex_it : !gsubop.graph_set_iterator<["all"]>]>]> @nodes::@set({type = !gsubop.node_set<[vx_it : !gsubop.graph_set_iterator<["all"]>]>}), @edges::@set({type = !gsubop.edge_set<[ex_it : !gsubop.graph_set_iterator<["all"]>]>})
            %vx = subop.nested_map %g_scan [@nodes::@set] (%arg0, %arg1){
                %node_stream = gsubop.scan_node_set %arg1 : !gsubop.node_set<[vx_it : !gsubop.graph_set_iterator<["all"]>]> @nodes::@ref({type = !gsubop.node_ref<[node_id : i32],[incoming : !gsubop.edge_set<[incoming_it : !gsubop.graph_set_iterator<["incoming"]>]>],[outgoing : !gsubop.edge_set<[outgoing_it : !gsubop.graph_set_iterator<["outgoing"]>]>],[property : i64]>})
                tuples.return %node_stream : !tuples.tuplestream
            }
            %result_nodes = subop.gather %vx @nodes::@ref {node_id => @nodes::@id({type = i32})}
            %node_degrees = gsubop.node_degree %result_nodes, %g["all"] -> @graph::@degrees({type = i64}) : !gsubop.graph<[vx : !gsubop.node_set<[vx_it : !gsubop.graph_set_iterator<["all"]>]>],[ex : !gsubop.edge_set<[ex_it : !gsubop.graph_set_iterator<["all"]>]>]>

            %0 = subop.create !subop.result_table<[int32p0 : i32, int64p1 : i64]>
            subop.materialize %result_nodes {@nodes::@id => int32p0, @graph::@degrees => int64p1}, %0 : !subop.result_table<[int32p0 : i32, int64p1 : i64]>
            %res = subop.create_from ["int32", "int64"] %0 : !subop.result_table<[int32p0 : i32, int64p1 : i64]> -> !subop.local_table<[int32p0 : i32, int64p1 : i64], ["int32", "int64"]>
            subop.execution_group_return %res : !subop.local_table<[int32p0 : i32, int64p1 : i64], ["int32", "int64"]>
        
        } -> !subop.local_table<[int32n0 : i32, int64n1 : i64], ["int32", "int64"]>
        subop.set_result 0 %subop_result : !subop.local_table<[int32n0 : i32, int64n1 : i64], ["int32", "int64"]>
        return
    }
}