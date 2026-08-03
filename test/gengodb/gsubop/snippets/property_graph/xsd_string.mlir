module {
    func.func @main() {
    	%subop_result = subop.execution_group (){
            
            %g = gsubop.create_builtin_graph {builtin = propertygraph} : !gsubop.graph<[vx : !gsubop.node_set<[vx_it : !gsubop.graph_set_iterator<["all"]>]>],[ex : !gsubop.edge_set<[ex_it : !gsubop.graph_set_iterator<["all"]>]>]>
            %g_scan = gsubop.scan_graph %g : !gsubop.graph<[vx : !gsubop.node_set<[vx_it : !gsubop.graph_set_iterator<["all"]>]>],[ex : !gsubop.edge_set<[ex_it : !gsubop.graph_set_iterator<["all"]>]>]> @nodes::@set({type = !gsubop.node_set<[vx_it : !gsubop.graph_set_iterator<["all"]>]>}), @edges::@set({type = !gsubop.edge_set<[ex_it : !gsubop.graph_set_iterator<["all"]>]>})
            %vx = subop.nested_map %g_scan [@nodes::@set] (%arg0, %arg1){
                %node_stream = gsubop.scan_node_set %arg1 : !gsubop.node_set<[vx_it : !gsubop.graph_set_iterator<["all"]>]> @nodes::@ref({type = !gsubop.node_ref<[node_id : i32],[incoming : !gsubop.edge_set<[incoming_it : !gsubop.graph_set_iterator<["incoming"]>]>],[outgoing : !gsubop.edge_set<[outgoing_it : !gsubop.graph_set_iterator<["outgoing"]>]>],[property : !gsubop.property_set<[prop_it : !gsubop.graph_set_iterator<["node"]>]>]>})
                tuples.return %node_stream : !tuples.tuplestream
            }
            %vx_id = subop.gather %vx @nodes::@ref { node_id => @nodes::@id({type = i32}) }
            %px = subop.gather %vx_id @nodes::@ref { property => @props::@set({type = !gsubop.property_set<[prop_it : !gsubop.graph_set_iterator<["node"]>]>}) }
            %props = subop.nested_map %px [@props::@set] (%arg0, %arg1){
                %prop_stream0 = gsubop.scan_property_set %arg1 : !gsubop.property_set<[prop_it : !gsubop.graph_set_iterator<["node"]>]> @props::@refs({type = !gsubop.property_ref})
                %prop_stream1 = gsubop.ref_to_str %prop_stream0 @props::@refs -> @props::@result({type = !db.string})
                tuples.return %prop_stream1 : !tuples.tuplestream
            }

            %0 = subop.create !subop.result_table<[int32p0 : i32, strp1 : !db.string]>
            subop.materialize %props {@nodes::@id => int32p0, @props::@result => strp1}, %0 : !subop.result_table<[int32p0 : i32, strp1 : !db.string]>
            %res = subop.create_from ["int32", "str"] %0 : !subop.result_table<[int32p0 : i32, strp1 : !db.string]> -> !subop.local_table<[int32p0 : i32, strp1 : !db.string], ["int32", "str"]>
            subop.execution_group_return %res : !subop.local_table<[int32p0 : i32, strp1 : !db.string], ["int32", "str"]>
        
        } -> !subop.local_table<[int32n0 : i32, strn1 : !db.string], ["int32", "str"]>
        subop.set_result 0 %subop_result : !subop.local_table<[int32n0 : i32, strn1 : !db.string], ["int32", "str"]>
        return
    }
}