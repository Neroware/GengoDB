module {
    func.func @main() {
    	%subop_result = subop.execution_group (){
            
            %g = gsubop.create_builtin_graph {builtin = standard} : !gsubop.graph<[vx : !gsubop.node_set<[vx_it : !gsubop.graph_set_iterator<["all"]>]>],[ex : !gsubop.edge_set<[ex_it : !gsubop.graph_set_iterator<["all"]>]>]>
            %g_scan = gsubop.scan_graph %g : !gsubop.graph<[vx : !gsubop.node_set<[vx_it : !gsubop.graph_set_iterator<["all"]>]>],[ex : !gsubop.edge_set<[ex_it : !gsubop.graph_set_iterator<["all"]>]>]> @nodes::@set({type = !gsubop.node_set<[vx_it : !gsubop.graph_set_iterator<["all"]>]>}), @edges::@set({type = !gsubop.edge_set<[ex_it : !gsubop.graph_set_iterator<["all"]>]>})
            %vx = subop.nested_map %g_scan [@nodes::@set] (%arg0, %arg1){
                %node_stream = gsubop.scan_node_set %arg1 : !gsubop.node_set<[vx_it : !gsubop.graph_set_iterator<["all"]>]> @nodes::@ref({type = !gsubop.node_ref<[node_id : i32],[incoming : !gsubop.edge_set<[incoming_it : !gsubop.graph_set_iterator<["incoming"]>]>],[outgoing : !gsubop.edge_set<[outgoing_it : !gsubop.graph_set_iterator<["outgoing"]>]>],[property : i64]>})
                tuples.return %node_stream : !tuples.tuplestream
            }
            %result_nodes = subop.gather %vx @nodes::@ref {property => @nodes::@property({type = i64})}

            %0 = subop.create !subop.result_table<[int64p0 : i64]>
            subop.materialize %result_nodes {@nodes::@property => int64p0}, %0 : !subop.result_table<[int64p0 : i64]>
            %res = subop.create_from ["int64"] %0 : !subop.result_table<[int64p0 : i64]> -> !subop.local_table<[int64p0 : i64], ["int64"]>
            subop.execution_group_return %res : !subop.local_table<[int64p0 : i64], ["int64"]>
        
        } -> !subop.local_table<[int64n0 : i64], ["int64"]>
        subop.set_result 0 %subop_result : !subop.local_table<[int64n0 : i64], ["int64"]>
        return
    }
}