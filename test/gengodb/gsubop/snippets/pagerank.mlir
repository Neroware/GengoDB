module {
    func.func @main() {
        %subop_result = subop.execution_group (){

            %graph = gsubop.create_builtin_graph {builtin = pagerank} : !gsubop.graph<[vx : !gsubop.node_set<[vx_it : !gsubop.graph_set_iterator<["all"]>]>],[ex : !gsubop.edge_set<[ex_it : !gsubop.graph_set_iterator<["all"]>]>]>
            %step0 = subop.step %graph : !gsubop.graph<[vx : !gsubop.node_set<[vx_it : !gsubop.graph_set_iterator<["all"]>]>],[ex : !gsubop.edge_set<[ex_it : !gsubop.graph_set_iterator<["all"]>]>]> (%g0) -> !gsubop.graph<[vx : !gsubop.node_set<[vx_it : !gsubop.graph_set_iterator<["all"]>]>],[ex : !gsubop.edge_set<[ex_it : !gsubop.graph_set_iterator<["all"]>]>]> {
                %graph_scan = gsubop.scan_graph %g0 : !gsubop.graph<[vx : !gsubop.node_set<[vx_it : !gsubop.graph_set_iterator<["all"]>]>],[ex : !gsubop.edge_set<[ex_it : !gsubop.graph_set_iterator<["all"]>]>]> @nodes::@set({type = !gsubop.node_set<[vx_it : !gsubop.graph_set_iterator<["all"]>]>}), @edges::@set({type = !gsubop.edge_set<[ex_it : !gsubop.graph_set_iterator<["all"]>]>})
                %vertex_count = gsubop.node_count %graph_scan, %g0 -> @graph::@numVertices({type = index}) : !gsubop.graph<[vx : !gsubop.node_set<[vx_it : !gsubop.graph_set_iterator<["all"]>]>],[ex : !gsubop.edge_set<[ex_it : !gsubop.graph_set_iterator<["all"]>]>]>
                %node_refs = subop.nested_map %vertex_count [@nodes::@set] (%arg0, %arg1){
                    %node_stream = gsubop.scan_node_set %arg1 : !gsubop.node_set<[vx_it : !gsubop.graph_set_iterator<["all"]>]> @nodes::@ref({type = !gsubop.node_ref<[node_id_0 : i32],[incoming_0 : !gsubop.edge_set<[incoming_it_0 : !gsubop.graph_set_iterator<["incoming"]>]>],[outgoing_0 : !gsubop.edge_set<[outgoing_it_0: !gsubop.graph_set_iterator<["outgoing"]>]>],[rank_0 : f64, nextRank_0 : f64, l_0 : i32]>})
                    tuples.return %node_stream : !tuples.tuplestream
                }
                %gather_outgoing = subop.gather %node_refs @nodes::@ref {outgoing_0 => @nodes::@outgoing({type = !gsubop.edge_set<[outgoing_it_0 : !gsubop.graph_set_iterator<["outgoing"]>]>})}
                %gather_rank = subop.gather %gather_outgoing @nodes::@ref { rank_0 => @nodes::@rank({type = f64}) }
                %gather_l = subop.gather %gather_rank @nodes::@ref { l_0 => @nodes::@l({type = i32}) }
                %outgoing_refs = subop.nested_map %gather_l [@nodes::@outgoing] (%arg0, %arg1){
                    %edge_stream = gsubop.scan_edge_set %arg1 : !gsubop.edge_set<[outgoing_it_0 : !gsubop.graph_set_iterator<["outgoing"]>]> @edges::@ref({type = !gsubop.edge_ref<[edge_id_0 : i32],[from_0: !gsubop.node_ref<[node_id_1 : i32],[incoming_1 : !gsubop.edge_set<[incoming_it_1 : !gsubop.graph_set_iterator<["incoming"]>]>],[outgoing_1 : !gsubop.edge_set<[outgoing_it_1 : !gsubop.graph_set_iterator<["outgoing"]>]>],[rank_1 : f64, nextRank_1 : f64, l_1 : i32]>],[to_0 : !gsubop.node_ref<[node_id_2 : i32],[incoming_2 : !gsubop.edge_set<[incoming_it_2 : !gsubop.graph_set_iterator<["incoming"]>]>],[outgoing_2 : !gsubop.edge_set<[outgoing_it_2 : !gsubop.graph_set_iterator<["outgoing"]>]>],[rank_2 : f64, nextRank_2 : f64, l_2 : i32]>],[eprop_0 : i64]>})
                    tuples.return %edge_stream : !tuples.tuplestream
                }
                subop.reduce %outgoing_refs @nodes::@ref [@graph::@numVertices] ["rank_0", "l_0"] ([%totalVertices],[%currRank, %currL]) {
                    %c1 = arith.constant 1 : i32
                    %newL = arith.addi %currL, %c1 : i32
                    %c1f = arith.constant 1.0 : f64
                    %totalVerticesI64 = arith.index_cast %totalVertices : index to i64
                    %totalVerticesf = arith.uitofp %totalVerticesI64 : i64 to f64
                    %newRank = arith.divf %c1f, %totalVerticesf : f64

                    tuples.return %newRank, %newL : f64, i32
                }
                subop.step_return %g0 : !gsubop.graph<[vx : !gsubop.node_set<[vx_it : !gsubop.graph_set_iterator<["all"]>]>],[ex : !gsubop.edge_set<[ex_it : !gsubop.graph_set_iterator<["all"]>]>]>
            }
            %step1 = subop.step %step0 : !gsubop.graph<[vx : !gsubop.node_set<[vx_it : !gsubop.graph_set_iterator<["all"]>]>],[ex : !gsubop.edge_set<[ex_it : !gsubop.graph_set_iterator<["all"]>]>]> (%g1) -> !gsubop.graph<[vx : !gsubop.node_set<[vx_it : !gsubop.graph_set_iterator<["all"]>]>],[ex : !gsubop.edge_set<[ex_it : !gsubop.graph_set_iterator<["all"]>]>]> {
                %ctr = subop.create_simple_state !subop.simple_state<[ctr : i32]> initial: {
                    %c0 = db.constant(0) : i32
                    tuples.return %c0 : i32
                }
                %zero = subop.create_simple_state !subop.simple_state<[zero : i32]> initial: {
                    %c0 = db.constant(0) : i32
                    tuples.return %c0 : i32
                }
                %loop = subop.loop %zero : !subop.simple_state<[zero : i32]> (%arg) -> !subop.simple_state<[loop : i32]> {
                    %loopstep0 = subop.step %g1 : !gsubop.graph<[vx : !gsubop.node_set<[vx_it : !gsubop.graph_set_iterator<["all"]>]>],[ex : !gsubop.edge_set<[ex_it : !gsubop.graph_set_iterator<["all"]>]>]> (%g2) -> !gsubop.graph<[vx : !gsubop.node_set<[vx_it : !gsubop.graph_set_iterator<["all"]>]>],[ex : !gsubop.edge_set<[ex_it : !gsubop.graph_set_iterator<["all"]>]>]> {
                        %iLStream0 = gsubop.scan_graph %g2 : !gsubop.graph<[vx : !gsubop.node_set<[vx_it : !gsubop.graph_set_iterator<["all"]>]>],[ex : !gsubop.edge_set<[ex_it : !gsubop.graph_set_iterator<["all"]>]>]> @nodes0::@set({type = !gsubop.node_set<[vx_it : !gsubop.graph_set_iterator<["all"]>]>}), @edges0::@set({type = !gsubop.edge_set<[ex_it : !gsubop.graph_set_iterator<["all"]>]>})
                        %iLStream1 = gsubop.node_count %iLStream0, %g2 -> @graph0::@numVertices({type = index}) : !gsubop.graph<[vx : !gsubop.node_set<[vx_it : !gsubop.graph_set_iterator<["all"]>]>],[ex : !gsubop.edge_set<[ex_it : !gsubop.graph_set_iterator<["all"]>]>]>
                        %iLStream2 = subop.nested_map %iLStream1 [@nodes0::@set] (%arg0, %arg1){
                            %node_stream = gsubop.scan_node_set %arg1 : !gsubop.node_set<[vx_it : !gsubop.graph_set_iterator<["all"]>]> @nodes0::@ref({type = !gsubop.node_ref<[node_id_2 : i32],[incoming_2 : !gsubop.edge_set<[incoming_it_2 : !gsubop.graph_set_iterator<["incoming"]>]>],[outgoing_2 : !gsubop.edge_set<[outgoing_it_2: !gsubop.graph_set_iterator<["outgoing"]>]>],[rank_1 : f64, nextRank_1 : f64, l_1 : i32]>})
                            tuples.return %node_stream : !tuples.tuplestream
                        }
                        %iLStream3 = subop.map %iLStream2 computes: [@nodes0::@initialRank({type=f64})] input:[@graph0::@numVertices] (%totalVertices : index){
                            %totalVerticesI64 = arith.index_cast %totalVertices : index to i64
                            %totalVerticesf = arith.uitofp %totalVerticesI64 : i64 to f64
                            %c15 = arith.constant 0.15 : f64
                            %initialRank = arith.divf %c15,%totalVerticesf : f64
                            tuples.return %initialRank : f64
                        }
                        subop.scatter %iLStream3 @nodes0::@ref { @nodes0::@initialRank => nextRank_1 }
                        subop.step_return %g2 : !gsubop.graph<[vx : !gsubop.node_set<[vx_it : !gsubop.graph_set_iterator<["all"]>]>],[ex : !gsubop.edge_set<[ex_it : !gsubop.graph_set_iterator<["all"]>]>]>
                    }
                    %loopstep1 = subop.step %loopstep0 : !gsubop.graph<[vx : !gsubop.node_set<[vx_it : !gsubop.graph_set_iterator<["all"]>]>],[ex : !gsubop.edge_set<[ex_it : !gsubop.graph_set_iterator<["all"]>]>]> (%g3) -> !gsubop.graph<[vx : !gsubop.node_set<[vx_it : !gsubop.graph_set_iterator<["all"]>]>],[ex : !gsubop.edge_set<[ex_it : !gsubop.graph_set_iterator<["all"]>]>]> {
                        %iLStream4 = gsubop.scan_graph %g3 : !gsubop.graph<[vx : !gsubop.node_set<[vx_it : !gsubop.graph_set_iterator<["all"]>]>],[ex : !gsubop.edge_set<[ex_it : !gsubop.graph_set_iterator<["all"]>]>]> @nodes0::@set({type = !gsubop.node_set<[vx_it : !gsubop.graph_set_iterator<["all"]>]>}), @edges0::@set({type = !gsubop.edge_set<[ex_it : !gsubop.graph_set_iterator<["all"]>]>})
                        %iLStream5 = subop.nested_map %iLStream4 [@nodes0::@set] (%arg0, %arg1){
                            %node_stream = gsubop.scan_node_set %arg1 : !gsubop.node_set<[vx_it : !gsubop.graph_set_iterator<["all"]>]> @nodes0::@ref({type = !gsubop.node_ref<[node_id_2 : i32],[incoming_2 : !gsubop.edge_set<[incoming_it_2 : !gsubop.graph_set_iterator<["incoming"]>]>],[outgoing_2 : !gsubop.edge_set<[outgoing_it_2: !gsubop.graph_set_iterator<["outgoing"]>]>],[rank_1 : f64, nextRank_1 : f64, l_1 : i32]>})
                            tuples.return %node_stream : !tuples.tuplestream
                        }
                        %iLStream6 = subop.gather %iLStream5 @nodes0::@ref {outgoing_2 => @nodes0::@outgoing({type = !gsubop.edge_set<[outgoing_it_1 : !gsubop.graph_set_iterator<["outgoing"]>]>})}
                        %iLStream7 = subop.nested_map %iLStream6 [@nodes0::@outgoing] (%arg0, %arg1){
                            %edge_stream = gsubop.scan_edge_set %arg1 : !gsubop.edge_set<[outgoing_it_1 : !gsubop.graph_set_iterator<["outgoing"]>]> @edges0::@ref({type = !gsubop.edge_ref<[edge_id_1 : i32],[from_1: !gsubop.node_ref<[node_id_3 : i32],[incoming_3 : !gsubop.edge_set<[incoming_it_3 : !gsubop.graph_set_iterator<["incoming"]>]>],[outgoing_3 : !gsubop.edge_set<[outgoing_it_3 : !gsubop.graph_set_iterator<["outgoing"]>]>],[rank_3 : f64, nextRank_3 : f64, l_3 : i32]>],[to_1 : !gsubop.node_ref<[node_id_4 : i32],[incoming_4 : !gsubop.edge_set<[incoming_it_4 : !gsubop.graph_set_iterator<["incoming"]>]>],[outgoing_4 : !gsubop.edge_set<[outgoing_it_4 : !gsubop.graph_set_iterator<["outgoing"]>]>],[rank_4 : f64, nextRank_4 : f64, l_4 : i32]>],[eprop_1 : i64]>})
                            tuples.return %edge_stream : !tuples.tuplestream
                        }
                        %iLStream8 = subop.gather %iLStream7 @edges0::@ref { to_1 => @edges0::@to({type = !gsubop.node_ref<[node_id_4 : i32],[incoming_4 : !gsubop.edge_set<[incoming_it_4 : !gsubop.graph_set_iterator<["incoming"]>]>],[outgoing_4 : !gsubop.edge_set<[outgoing_it_4 : !gsubop.graph_set_iterator<["outgoing"]>]>],[rank_4 : f64, nextRank_4 : f64, l_4 : i32]>}) }
                        %iLStream9 = subop.gather %iLStream8 @nodes0::@ref { rank_1 => @nodes0::@rank({type = f64}) }
                        %iLStream10 = subop.gather %iLStream9 @nodes0::@ref { l_1 => @nodes0::@l({type = i32}) }
                        subop.reduce %iLStream10 @edges0::@to [@nodes0::@rank,@nodes0::@l] ["nextRank_4"] ([%currRank,%currL],[%rank]){
                            %c085 = arith.constant 0.85 : f64
                            %c1 = arith.constant 1 : i32
                            %safeL = arith.maxui %c1, %currL :i32
                            %currLF= arith.uitofp %safeL : i32 to f64
                            %toAdd = arith.divf %currRank, %currLF : f64
                            %damped= arith.mulf %toAdd, %c085 : f64
                            %newRank = arith.addf %rank, %damped : f64
                            tuples.return %newRank : f64
                        }
                        subop.step_return %g3 : !gsubop.graph<[vx : !gsubop.node_set<[vx_it : !gsubop.graph_set_iterator<["all"]>]>],[ex : !gsubop.edge_set<[ex_it : !gsubop.graph_set_iterator<["all"]>]>]>
                    }
                    %one = subop.create_simple_state !subop.simple_state<[one : i32]> initial: {
                        %c1 = db.constant(1) : i32
                        tuples.return %c1 : i32
                    }

                    %iLStream11 = gsubop.scan_graph %loopstep1 : !gsubop.graph<[vx : !gsubop.node_set<[vx_it : !gsubop.graph_set_iterator<["all"]>]>],[ex : !gsubop.edge_set<[ex_it : !gsubop.graph_set_iterator<["all"]>]>]> @nodes0::@set({type = !gsubop.node_set<[vx_it : !gsubop.graph_set_iterator<["all"]>]>}), @edges0::@set({type = !gsubop.edge_set<[ex_it : !gsubop.graph_set_iterator<["all"]>]>})
                    %iLStream12 = subop.nested_map %iLStream11 [@nodes0::@set] (%arg0, %arg1){
                        %node_stream = gsubop.scan_node_set %arg1 : !gsubop.node_set<[vx_it : !gsubop.graph_set_iterator<["all"]>]> @nodes0::@ref({type = !gsubop.node_ref<[node_id_2 : i32],[incoming_2 : !gsubop.edge_set<[incoming_it_2 : !gsubop.graph_set_iterator<["incoming"]>]>],[outgoing_2 : !gsubop.edge_set<[outgoing_it_2: !gsubop.graph_set_iterator<["outgoing"]>]>],[rank_1 : f64, nextRank_1 : f64, l_1 : i32]>})
                        tuples.return %node_stream : !tuples.tuplestream
                    }
                    %iLStream13 = subop.gather %iLStream12 @nodes0::@ref { nextRank_1 => @nodes0::@nextRank({type = i32}) }
                    subop.scatter %iLStream13 @nodes0::@ref { @nodes0::@nextRank => rank_1 }

                    %shouldContinue = subop.create_simple_state !subop.simple_state<[shouldContinue:i1]> initial: {
                        %false = arith.constant 0 : i1
                        tuples.return %false : i1
                    }
                    %20 = subop.scan_refs %ctr : !subop.simple_state<[ctr:i32]> @s::@ref({type=!subop.entry_ref<!subop.simple_state<[ctr:i32]>>})
                    %21 = subop.gather %20 @s::@ref {ctr=> @s::@ctr({type=i32})}
                    %s23 = subop.map %21 computes: [@m::@p1({type=i32}),@m::@continue({type=i1})] input: [@s::@ctr] (%ctrVal : i32){
                        %c1 = db.constant(1) : i32
                        %p1 = arith.addi %c1, %ctrVal : i32
                        %c5 = arith.constant 1000 : i32
                        %p1Lt5 = arith.cmpi slt, %p1, %c5 : i32
                        tuples.return %p1, %p1Lt5 : i32,i1
                    }
                    %s24 = subop.lookup %s23 %shouldContinue[] : !subop.simple_state<[shouldContinue:i1]> @ls::@ref({type=!subop.entry_ref<!subop.simple_state<[shouldContinue:i1]>>})
                    subop.scatter %s23 @s::@ref {@m::@p1 => ctr}
                    subop.scatter %s24 @ls::@ref {@m::@continue => shouldContinue}
                    subop.loop_continue (%shouldContinue:  !subop.simple_state<[shouldContinue:i1]>["shouldContinue"]) %one : !subop.simple_state<[one : i32]>
                }
                subop.step_return %g1 : !gsubop.graph<[vx : !gsubop.node_set<[vx_it : !gsubop.graph_set_iterator<["all"]>]>],[ex : !gsubop.edge_set<[ex_it : !gsubop.graph_set_iterator<["all"]>]>]>
            }
            
            %result_graph_scan = gsubop.scan_graph %step1 : !gsubop.graph<[vx : !gsubop.node_set<[vx_it : !gsubop.graph_set_iterator<["all"]>]>],[ex : !gsubop.edge_set<[ex_it : !gsubop.graph_set_iterator<["all"]>]>]> @nodes1::@set({type = !gsubop.node_set<[vx_it : !gsubop.graph_set_iterator<["all"]>]>}), @edges::@set({type = !gsubop.edge_set<[ex_it : !gsubop.graph_set_iterator<["all"]>]>})
            %result_node_refs = subop.nested_map %result_graph_scan [@nodes1::@set] (%arg0, %arg1){
                %node_stream = gsubop.scan_node_set %arg1 : !gsubop.node_set<[vx_it : !gsubop.graph_set_iterator<["all"]>]> @nodes1::@ref({type = !gsubop.node_ref<[node_id_1 : i32],[incoming_1 : !gsubop.edge_set<[incoming_it_1 : !gsubop.graph_set_iterator<["incoming"]>]>],[outgoing_1 : !gsubop.edge_set<[outgoing_it_1 : !gsubop.graph_set_iterator<["outgoing"]>]>],[rank_0 : f64, nextRank_0 : f64, l_0 : i32]>})
                tuples.return %node_stream : !tuples.tuplestream
            }
            %result_node_ids = subop.gather %result_node_refs @nodes1::@ref {node_id_1 => @nodes1::@id({type = i32})}
            %result_node_ranks = subop.gather %result_node_ids @nodes1::@ref {rank_0 => @nodes1::@rank({type = f64})}
            %result_node_l = subop.gather %result_node_ranks @nodes1::@ref {l_0 => @nodes1::@l({type = i32})}
            %result_table = subop.create !subop.result_table<[id0 : i32, rank0 : f64, l0 : i32]>
            subop.materialize %result_node_l {@nodes1::@id => id0, @nodes1::@rank => rank0, @nodes1::@l => l0}, %result_table : !subop.result_table<[id0 : i32, rank0 : f64, l0 : i32]>
            %local_table = subop.create_from ["id", "rank", "l"] %result_table : !subop.result_table<[id0 : i32, rank0 : f64, l0 : i32]> -> !subop.local_table<[id0 : i32, rank0 : f64, l0 : i32],["id", "rank", "l"]>
            subop.execution_group_return %local_table : !subop.local_table<[id0 : i32, rank0 : f64, l0 : i32],["id", "rank", "l"]>
        } -> !subop.local_table<[id0 : i32, rank0 : f64, l0 : i32],["id", "rank", "l"]>
        subop.set_result 0 %subop_result  : !subop.local_table<[id0 : i32, rank0 : f64, l0 : i32],["id", "rank", "l"]>
        return
    }
}