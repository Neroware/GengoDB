// RUN: run-mlir %s %S/../../../resources/ttl/coffee | FileCheck %s

// Arithmetic and comparison on a real graph-loaded xsd:Integer literal (see
// coffee.ttl's `ex:bob ex:customerId "123456789012345678901234567890"^^xsd:integer`,
// 30 digits, well beyond int64_t/uint64_t range -- proves no truncation
// happens end-to-end through CreateNodeRefOpLowering's canonicalization and
// ArithOpLowering's blobNumericArith fallback). Every node in the graph is
// visited (see string_cast_graph.mlir for the same scan-all-nodes pattern);
// only the customerId literal is Integer-tagged, so it's the only one whose
// arithmetic result is non-empty/non-Unspecified.

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

                // Same-tag self-comparison: a regression guard for
                // compareBlobLiteral's signature simplification (see
                // Variant.cpp) -- must still report equal for every node,
                // Integer-tagged or not.
                %selfEq = variant.cmp eq %v, %v -> !db.nullable<i1>
                db.runtime_call "DumpValue" (%selfEq) : (!db.nullable<i1>) -> ()

                // Cross-tag arithmetic against a Long-tagged constant --
                // exercises blobNumericArith end-to-end against real
                // graph-resident (not synthetic create_scalar) Integer data.
                %one = arith.constant 1 : i64
                %vOne = variant.create_scalar %one : i64
                %sum = variant.arith add %v, %vOne
                %sumStr = variant.to_string %sum -> !db.string
                db.runtime_call "DumpValue" (%sumStr) : (!db.string) -> ()

                tuples.return %sumStr : !db.string
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

// Every node self-compares equal, Integer-tagged or not.
// CHECK-DAG: bool(true)
// The customerId literal (123456789012345678901234567890 + 1), no truncation.
// CHECK-DAG: string("123456789012345678901234567891")
