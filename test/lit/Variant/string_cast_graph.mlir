// RUN: run-mlir %s %S/../../../resources/ttl/coffee | FileCheck %s

module {
    func.func @main() {
        %subop_result = subop.execution_group (){
            %g = gsubop.get_external_graph {name = "coffee", id = "file://resources/ttl/coffee/coffee.ttl#rdf"} : !gsubop.graph<[vx : !gsubop.node_set<[vx_it : !gsubop.graph_set_iterator<["all"]>]>],[ex : !gsubop.edge_set<[ex_it : !gsubop.graph_set_iterator<["all"]>]>]>
            %g_scan = gsubop.scan_graph %g : !gsubop.graph<[vx : !gsubop.node_set<[vx_it : !gsubop.graph_set_iterator<["all"]>]>],[ex : !gsubop.edge_set<[ex_it : !gsubop.graph_set_iterator<["all"]>]>]> @nodes::@set({type = !gsubop.node_set<[vx_it : !gsubop.graph_set_iterator<["all"]>]>}), @edges::@set({type = !gsubop.edge_set<[ex_it : !gsubop.graph_set_iterator<["all"]>]>})
            %vx = subop.nested_map %g_scan [@nodes::@set] (%arg0, %arg1){
                %node_stream = gsubop.scan_node_set %arg1 : !gsubop.node_set<[vx_it : !gsubop.graph_set_iterator<["all"]>]> @nodes::@ref({type = !gsubop.node_ref<[node_id : i32],[incoming : !gsubop.edge_set<[incoming_it : !gsubop.graph_set_iterator<["incoming"]>]>],[outgoing : !gsubop.edge_set<[outgoing_it : !gsubop.graph_set_iterator<["outgoing"]>]>],[property : !gsubop.property_ref]>})
                tuples.return %node_stream : !tuples.tuplestream
            }
            %with_raw = subop.map %vx computes: [@nodes::@raw({type = !util.ref<i8>})] input: [@nodes::@ref] (%nodeRef : !gsubop.node_ref<[node_id : i32],[incoming : !gsubop.edge_set<[incoming_it : !gsubop.graph_set_iterator<["incoming"]>]>],[outgoing : !gsubop.edge_set<[outgoing_it : !gsubop.graph_set_iterator<["outgoing"]>]>],[property : !gsubop.property_ref]>){
                %raw = builtin.unrealized_conversion_cast %nodeRef : !gsubop.node_ref<[node_id : i32],[incoming : !gsubop.edge_set<[incoming_it : !gsubop.graph_set_iterator<["incoming"]>]>],[outgoing : !gsubop.edge_set<[outgoing_it : !gsubop.graph_set_iterator<["outgoing"]>]>],[property : !gsubop.property_ref]> to !util.ref<i8>
                tuples.return %raw : !util.ref<i8>
            }
            %with_str = subop.map %with_raw computes: [@nodes::@str({type = !db.string})] input: [@nodes::@raw] (%raw : !util.ref<i8>){
                %v = variant.create_node_ref %raw : !util.ref<i8>

                // STR() on an RDFNode/blob-literal ref should degenerate to
                // exactly variant.to_string's output
                %direct = variant.to_string %v -> !db.string
                db.runtime_call "DumpValue" (%direct) : (!db.string) -> ()
                %strCast = variant.str_cast %v
                %strCastStr = variant.to_string %strCast -> !db.string
                db.runtime_call "DumpValue" (%strCastStr) : (!db.string) -> ()

                // A numeric cast of an RDFNode ref
                %intTag = arith.constant 204 : i32
                %unspecifiedTag = arith.constant 0 : i32
                %casted = variant.cast %v { type = %intTag }
                %castedIsUnspecified = variant.variant_is_a %casted { type = %unspecifiedTag }
                db.runtime_call "DumpValue" (%castedIsUnspecified) : (i1) -> ()

                tuples.return %strCastStr : !db.string
            }
            %result_nodes = subop.gather %with_str @nodes::@ref {node_id => @nodes::@id({type = i32})}

            %0 = subop.create !subop.result_table<[int32p0 : i32]>
            subop.materialize %result_nodes {@nodes::@id => int32p0}, %0 : !subop.result_table<[int32p0 : i32]>
            %res = subop.create_from ["int32"] %0 : !subop.result_table<[int32p0 : i32]> -> !subop.local_table<[int32p0 : i32], ["int32"]>
            subop.execution_group_return %res : !subop.local_table<[int32p0 : i32], ["int32"]>
        } -> !subop.local_table<[int32n0 : i32], ["int32"]>
        subop.set_result 0 %subop_result : !subop.local_table<[int32n0 : i32], ["int32"]>
        return
    }
}

// CHECK-DAG: string("<http://example.org/bob>")
// CHECK-DAG: string("_:cup1")
// CHECK-DAG: string("Coffee")
// CHECK-DAG: bool(true)
