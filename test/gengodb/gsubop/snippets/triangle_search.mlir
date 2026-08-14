// Triangle search on a directed graph
//   0 -> 2      1 -> 0      1 -> 2
//   1 -> 4      2 -> 4      2 -> 3
//
//
// Triangles: (1,0,2) via 1->0, 0->2, 1->2
//            (1,2,4) via 1->2, 2->4, 1->4
module {
    func.func @main() {
        %subop_result = subop.execution_group (){
            %g = gsubop.create_builtin_graph {builtin = standard} : !gsubop.graph<[vx : !gsubop.node_set<[vx_it : !gsubop.graph_set_iterator<["all"]>]>],[ex : !gsubop.edge_set<[ex_it : !gsubop.graph_set_iterator<["all"]>]>]>
            %g_scan = gsubop.scan_graph %g : !gsubop.graph<[vx : !gsubop.node_set<[vx_it : !gsubop.graph_set_iterator<["all"]>]>],[ex : !gsubop.edge_set<[ex_it : !gsubop.graph_set_iterator<["all"]>]>]> @nodes::@set({type = !gsubop.node_set<[vx_it : !gsubop.graph_set_iterator<["all"]>]>}), @edges::@set({type = !gsubop.edge_set<[ex_it : !gsubop.graph_set_iterator<["all"]>]>})

            // vertex a
            %a_refs = subop.nested_map %g_scan [@nodes::@set] (%arg0, %arg1){
                %node_stream = gsubop.scan_node_set %arg1 : !gsubop.node_set<[vx_it : !gsubop.graph_set_iterator<["all"]>]> @a::@ref({type = !gsubop.node_ref<[node_id : i32],[incoming : !gsubop.edge_set<[incoming_it : !gsubop.graph_set_iterator<["incoming"]>]>],[outgoing : !gsubop.edge_set<[outgoing_it : !gsubop.graph_set_iterator<["outgoing"]>]>],[property : i64]>})
                tuples.return %node_stream : !tuples.tuplestream
            }
            %a_id = subop.gather %a_refs @a::@ref {node_id => @a::@id({type = i32})}
            %a_out = subop.gather %a_id @a::@ref {outgoing => @a::@outgoing({type = !gsubop.edge_set<[outgoing_it : !gsubop.graph_set_iterator<["outgoing"]>]>})}

            // hop 1: edge a->b, walking a's outgoing edges
            %ab_refs = subop.nested_map %a_out [@a::@outgoing] (%arg0, %arg1){
                %edge_stream = gsubop.scan_edge_set %arg1 : !gsubop.edge_set<[outgoing_it : !gsubop.graph_set_iterator<["outgoing"]>]> @ab::@ref({type = !gsubop.edge_ref<[edge_id : i32],[from: !gsubop.node_ref<[node_id1 : i32],[incoming1 : !gsubop.edge_set<[incoming_it1 : !gsubop.graph_set_iterator<["incoming"]>]>],[outgoing1 : !gsubop.edge_set<[outgoing_it1 : !gsubop.graph_set_iterator<["outgoing"]>]>],[property1 : i64]>],[to : !gsubop.node_ref<[node_id2 : i32],[incoming2 : !gsubop.edge_set<[incoming_it2 : !gsubop.graph_set_iterator<["incoming"]>]>],[outgoing2 : !gsubop.edge_set<[outgoing_it2 : !gsubop.graph_set_iterator<["outgoing"]>]>],[property2 : i64]>],[edge_prop : i64]>})
                tuples.return %edge_stream : !tuples.tuplestream
            }
            %b_ref = subop.gather %ab_refs @ab::@ref {to => @b::@ref({type = !gsubop.node_ref<[node_id2 : i32],[incoming2 : !gsubop.edge_set<[incoming_it2 : !gsubop.graph_set_iterator<["incoming"]>]>],[outgoing2 : !gsubop.edge_set<[outgoing_it2 : !gsubop.graph_set_iterator<["outgoing"]>]>],[property2 : i64]>})}
            %b_id = subop.gather %b_ref @b::@ref {node_id2 => @b::@id({type = i32})}
            %b_out = subop.gather %b_id @b::@ref {outgoing2 => @b::@outgoing({type = !gsubop.edge_set<[outgoing_it2 : !gsubop.graph_set_iterator<["outgoing"]>]>})}

            // hop 2: edge b->c, walking b's outgoing edges
            %bc_refs = subop.nested_map %b_out [@b::@outgoing] (%arg0, %arg1){
                %edge_stream = gsubop.scan_edge_set %arg1 : !gsubop.edge_set<[outgoing_it2 : !gsubop.graph_set_iterator<["outgoing"]>]> @bc::@ref({type = !gsubop.edge_ref<[edge_id3 : i32],[from3: !gsubop.node_ref<[node_id3 : i32],[incoming3 : !gsubop.edge_set<[incoming_it3 : !gsubop.graph_set_iterator<["incoming"]>]>],[outgoing3 : !gsubop.edge_set<[outgoing_it3 : !gsubop.graph_set_iterator<["outgoing"]>]>],[property3 : i64]>],[to3 : !gsubop.node_ref<[node_id4 : i32],[incoming4 : !gsubop.edge_set<[incoming_it4 : !gsubop.graph_set_iterator<["incoming"]>]>],[outgoing4 : !gsubop.edge_set<[outgoing_it4 : !gsubop.graph_set_iterator<["outgoing"]>]>],[property4 : i64]>],[edge_prop3 : i64]>})
                tuples.return %edge_stream : !tuples.tuplestream
            }
            %c_ref = subop.gather %bc_refs @bc::@ref {to3 => @c::@ref({type = !gsubop.node_ref<[node_id4 : i32],[incoming4 : !gsubop.edge_set<[incoming_it4 : !gsubop.graph_set_iterator<["incoming"]>]>],[outgoing4 : !gsubop.edge_set<[outgoing_it4 : !gsubop.graph_set_iterator<["outgoing"]>]>],[property4 : i64]>})}
            %c_id = subop.gather %c_ref @c::@ref {node_id4 => @c::@id({type = i32})}

            // close the triangle: re-walk a's outgoing edges and keep only
            // the (a,b,c) triples where one of them targets c.
            %ac_refs = subop.nested_map %c_id [@a::@outgoing] (%arg0, %arg1){
                %edge_stream = gsubop.scan_edge_set %arg1 : !gsubop.edge_set<[outgoing_it : !gsubop.graph_set_iterator<["outgoing"]>]> @ac::@ref({type = !gsubop.edge_ref<[edge_id5 : i32],[from5: !gsubop.node_ref<[node_id5 : i32],[incoming5 : !gsubop.edge_set<[incoming_it5 : !gsubop.graph_set_iterator<["incoming"]>]>],[outgoing5 : !gsubop.edge_set<[outgoing_it5 : !gsubop.graph_set_iterator<["outgoing"]>]>],[property5 : i64]>],[to5 : !gsubop.node_ref<[node_id6 : i32],[incoming6 : !gsubop.edge_set<[incoming_it6 : !gsubop.graph_set_iterator<["incoming"]>]>],[outgoing6 : !gsubop.edge_set<[outgoing_it6 : !gsubop.graph_set_iterator<["outgoing"]>]>],[property6 : i64]>],[edge_prop5 : i64]>})
                tuples.return %edge_stream : !tuples.tuplestream
            }
            %ac_to_ref = subop.gather %ac_refs @ac::@ref {to5 => @acTo::@ref({type = !gsubop.node_ref<[node_id6 : i32],[incoming6 : !gsubop.edge_set<[incoming_it6 : !gsubop.graph_set_iterator<["incoming"]>]>],[outgoing6 : !gsubop.edge_set<[outgoing_it6 : !gsubop.graph_set_iterator<["outgoing"]>]>],[property6 : i64]>})}
            %ac_to_id = subop.gather %ac_to_ref @acTo::@ref {node_id6 => @ac::@toId({type = i32})}
            %matched = subop.map %ac_to_id computes: [@m::@isTriangle({type=i1})] input: [@ac::@toId, @c::@id] (%toId : i32, %cId : i32){
                %eq = arith.cmpi eq, %toId, %cId : i32
                tuples.return %eq : i1
            }
            %closed = subop.filter %matched all_true [@m::@isTriangle]

            %result_table = subop.create !subop.result_table<[a0:i32, b0:i32, c0:i32]>
            subop.materialize %closed {@a::@id => a0, @b::@id => b0, @c::@id => c0}, %result_table : !subop.result_table<[a0:i32,b0:i32,c0:i32]>
            %local_table = subop.create_from ["a","b","c"] %result_table : !subop.result_table<[a0:i32,b0:i32,c0:i32]> -> !subop.local_table<[a0:i32,b0:i32,c0:i32],["a","b","c"]>
            subop.execution_group_return %local_table : !subop.local_table<[a0:i32,b0:i32,c0:i32],["a","b","c"]>
        } -> !subop.local_table<[a0:i32,b0:i32,c0:i32],["a","b","c"]>
        subop.set_result 0 %subop_result : !subop.local_table<[a0:i32,b0:i32,c0:i32],["a","b","c"]>
        return
    }
}
