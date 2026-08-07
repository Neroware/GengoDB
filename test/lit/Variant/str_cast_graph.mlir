// RUN: run-mlir %s %S/../../../resources/ttl | FileCheck %s

// variant.str_cast / variant.cast against a *real*, RDF-catalog-backed graph
// (loaded from resources/ttl/coffee.ttl) -- mirrors graph_ref.mlir's
// hand-written get_external_graph + full node scan skeleton (see that file
// for why: no in-memory builtin-graph fixture carries real XSD-typed nodes).
// CHECK-DAG throughout since node-scan order is an implementation detail.
module {
    func.func @main() {
        %subop_result = subop.execution_group (){
            %g = gsubop.get_external_graph {name = "coffee", id = "file://resources/ttl/coffee.ttl#rdf"} : !gsubop.graph<[vx : !gsubop.node_set<[vx_it : !gsubop.graph_set_iterator<["all"]>]>],[ex : !gsubop.edge_set<[ex_it : !gsubop.graph_set_iterator<["all"]>]>]>
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
                // exactly variant.to_string's output -- the shared
                // computeLexicalForm dispatch, just repackaged as a variant.
                %direct = variant.to_string %v -> !db.string
                db.runtime_call "DumpValue" (%direct) : (!db.string) -> ()
                %strCast = variant.str_cast %v
                %strCastStr = variant.to_string %strCast -> !db.string
                db.runtime_call "DumpValue" (%strCastStr) : (!db.string) -> ()

                // A numeric cast of an RDFNode ref (IRI/blank node -- no
                // literal value to reconstruct) must fail -> Unspecified.
                // Dumped unconditionally for every scanned entry (literal or
                // RDFNode); the IRI/blank-node entries are the ones that must
                // contribute a `bool(true)` to the output.
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

// IRI (ex:bob): str_cast's lexical form matches to_string's directly.
// CHECK-DAG: string("<http://example.org/bob>")
// Blank node (_:cup1): same.
// CHECK-DAG: string("_:cup1")
// xsd:string literal ("Coffee"): blob-literal fallback path, exercised
// through the shared computeLexicalForm dispatch against real
// PropertyGraph storage (not just scratch-allocated scalars).
// CHECK-DAG: string("Coffee")
// Every RDFNode-tagged ref (IRI/blank node) hit during the scan reports its
// numeric cast attempt as Unspecified -- an IRI/blank-node source always
// fails a numeric cast, and the scan reaches at least one such ref.
// CHECK-DAG: bool(true)
