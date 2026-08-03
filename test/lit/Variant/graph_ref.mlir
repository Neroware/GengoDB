// RUN: run-mlir %s %S/../../../resources/ttl | FileCheck %s

// `variant.create_ref` against a *real*, RDF-catalog-backed graph (loaded
// from resources/ttl/coffee.ttl, the same fixture the GPM/SPARQL snippet
// tests use) -- as opposed to every other test in this directory, which only
// exercises `variant.create_scalar` on query-text-computed values.
//
// There is no in-memory/builtin gsubop graph fixture that carries real RDF
// term data (`gsubop.create_builtin_graph` is for generic graph-algorithm
// tests like pagerank and has no XSD-typed nodes), so this hand-writes plain
// gsubop/subop IR -- a `get_external_graph` + full node scan, no `gpm.*`
// ops at all -- and lets `run-mlir` drive it through the real pipeline,
// which now also runs `--lower-variant-to-std` (see Execution.cpp's
// SubOpLoweringStep) purely because this module happens to contain
// `variant.*` ops; no production query does today.
//
// Scanning every node (rather than filtering to one) is deliberate: it's the
// simplest way to reach an IRI, a blank node, and literal terms in one pass
// without relying on `gsubop.filter_by_identifier`, which only resolves
// IRI-shaped identifiers (verified separately -- it throws on a blank-node
// label). CHECK-DAG is used throughout since node-scan order is an
// implementation detail this test shouldn't depend on.
module {
    func.func @main() {
        %subop_result = subop.execution_group (){
            %g = gsubop.get_external_graph {name = "coffee", id = "file://resources/ttl/coffee.ttl#rdf"} : !gsubop.graph<[vx : !gsubop.node_set<[vx_it : !gsubop.graph_set_iterator<["all"]>]>],[ex : !gsubop.edge_set<[ex_it : !gsubop.graph_set_iterator<["all"]>]>]>
            %g_scan = gsubop.scan_graph %g : !gsubop.graph<[vx : !gsubop.node_set<[vx_it : !gsubop.graph_set_iterator<["all"]>]>],[ex : !gsubop.edge_set<[ex_it : !gsubop.graph_set_iterator<["all"]>]>]> @nodes::@set({type = !gsubop.node_set<[vx_it : !gsubop.graph_set_iterator<["all"]>]>}), @edges::@set({type = !gsubop.edge_set<[ex_it : !gsubop.graph_set_iterator<["all"]>]>})
            %vx = subop.nested_map %g_scan [@nodes::@set] (%arg0, %arg1){
                %node_stream = gsubop.scan_node_set %arg1 : !gsubop.node_set<[vx_it : !gsubop.graph_set_iterator<["all"]>]> @nodes::@ref({type = !gsubop.node_ref<[node_id : i32],[incoming : !gsubop.edge_set<[incoming_it : !gsubop.graph_set_iterator<["incoming"]>]>],[outgoing : !gsubop.edge_set<[outgoing_it : !gsubop.graph_set_iterator<["outgoing"]>]>],[property : !gsubop.property_ref]>})
                tuples.return %node_stream : !tuples.tuplestream
            }
            // `variant.create_ref` needs a raw `!util.ref<i8>`; a node_ref
            // column value is already a real pointer by this point (just
            // typed), so this is the same `unrealized_conversion_cast`
            // narrowing idiom `xsd.compare`'s lowering uses on graph refs.
            %with_raw = subop.map %vx computes: [@nodes::@raw({type = !util.ref<i8>})] input: [@nodes::@ref] (%nodeRef : !gsubop.node_ref<[node_id : i32],[incoming : !gsubop.edge_set<[incoming_it : !gsubop.graph_set_iterator<["incoming"]>]>],[outgoing : !gsubop.edge_set<[outgoing_it : !gsubop.graph_set_iterator<["outgoing"]>]>],[property : !gsubop.property_ref]>){
                %raw = builtin.unrealized_conversion_cast %nodeRef : !gsubop.node_ref<[node_id : i32],[incoming : !gsubop.edge_set<[incoming_it : !gsubop.graph_set_iterator<["incoming"]>]>],[outgoing : !gsubop.edge_set<[outgoing_it : !gsubop.graph_set_iterator<["outgoing"]>]>],[property : !gsubop.property_ref]> to !util.ref<i8>
                tuples.return %raw : !util.ref<i8>
            }
            %with_str = subop.map %with_raw computes: [@nodes::@str({type = !db.string})] input: [@nodes::@raw] (%raw : !util.ref<i8>){
                %v = variant.create_ref %raw : !util.ref<i8>
                %s = variant.to_string %v -> !db.string
                db.runtime_call "DumpValue" (%s) : (!db.string) -> ()
                // Zero-copy numeric unwrap: succeeds (with the real graph
                // payload) only for the xsd:long-tagged rows below; every
                // other tag (IRI/BNode/string-literal/...) must come back
                // null rather than reading garbage.
                %asLong = variant.variant_get_val %v -> !db.nullable<i64>
                db.runtime_call "DumpValue" (%asLong) : (!db.nullable<i64>) -> ()
                // Same idea for the rest of the fixed-width inlined family
                // (see RdfDatatypeInlineHelper/RdfDatatypeFixedHelper) --
                // each of these succeeds for exactly one real graph node
                // (ex:bob's xsd:byte/xsd:short/xsd:float properties) and
                // must come back null for every other row, never garbage.
                %asByte = variant.variant_get_val %v -> !db.nullable<i8>
                db.runtime_call "DumpValue" (%asByte) : (!db.nullable<i8>) -> ()
                %asShort = variant.variant_get_val %v -> !db.nullable<i16>
                db.runtime_call "DumpValue" (%asShort) : (!db.nullable<i16>) -> ()
                %asFloat = variant.variant_get_val %v -> !db.nullable<f32>
                db.runtime_call "DumpValue" (%asFloat) : (!db.nullable<f32>) -> ()
                tuples.return %s : !db.string
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

// IRI (ex:bob), rendered via the stack-scratch backend-id -> IRI reconstruction path.
// CHECK-DAG: string("<http://example.org/bob>")
// Blank node (_:cup1), rendered via the original graph pointer (identity = pointer, per RDF's blank-node-scoped-to-one-graph semantics).
// CHECK-DAG: string("_:cup1")
// xsd:long literal (ex:accountBalance "-987654321012345"), zero-copy: the ref points directly at PropertyData's i64 vector, no allocation.
// CHECK-DAG: string("-987654321012345")
// CHECK-DAG: int(-987654321012345)
// xsd:string literal ("Coffee"): to_string succeeds via the lexical-fallback path, but unwrapping it as i64 must be null (wrong tag), not garbage.
// CHECK-DAG: string("Coffee")
// CHECK-DAG: int(NULL)

// Rest of the fixed-width inlined family (RdfDatatypeInlineHelper's 32-bit
// property slot / RdfDatatypeFixedHelper's 64-bit slot), each verified via
// both to_string (works against real graph bytes regardless of tag) and a
// matching-type variant_get_val unwrap (only succeeds via the newly-added
// literalFromNumeric/extractNumericByTag cases -- these came back NULL
// before this coverage was added).
// xsd:byte (ex:temperatureOffset "-5")
// CHECK-DAG: string("-5")
// CHECK-DAG: int(-5)
// xsd:short (ex:queuePosition "300")
// CHECK-DAG: string("300")
// CHECK-DAG: int(300)
// xsd:float (ex:roomTemperature "21.5") -- XSD canonical form uses
// exponential notation (to_string), but DumpValue's own float formatting
// doesn't (variant_get_val's raw f32 unwrap).
// CHECK-DAG: string("2.15E1")
// CHECK-DAG: float(21.5)
// xsd:unsignedByte (ex:tableNumber "12"), xsd:unsignedInt (ex:visitCount
// "1200"), xsd:unsignedLong (ex:loyaltyPoints "18446744073709551615" -- the
// full uint64 range, only representable because extractNumericByTag/
// literalFromNumeric round-trip it as a *native* uint64, not a signed i64)
// -- not reachable via variant_get_val (no MLIR unsigned-integer type
// support, see tagForScalarType's note), so to_string is the only available
// end-to-end check for these three.
// CHECK-DAG: string("12")
// CHECK-DAG: string("1200")
// CHECK-DAG: string("18446744073709551615")
// xsd:date (ex:birthday "1999-11-04"): round-trips through
// RdfDatatypeFixedHelper::packDate/unpackDate, the same encoding
// RdfGraph.cpp itself uses for graph storage.
// CHECK-DAG: string("1999-11-04")
