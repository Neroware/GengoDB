// RUN: mlir-db-opt %s -split-input-file -verify-diagnostics -mlir-print-local-scope --to-graph-subop --to-graph-subop-scalars --lower-relalg-to-subop | FileCheck %s

// A variable subject anchor is resolved per tuple and expanded over its outgoing edges.
//CHECK-LABEL: func.func @subject_anchor
//CHECK: [[GRAPH:%.*]] = gsubop.get_external_graph{name = "coffee"
//CHECK-NOT: gsubop.get_external_graph
//CHECK: subop.nested_map %{{.*}} [] (%{{.*}}) {
//CHECK-NEXT: gsubop.scan_graph [[GRAPH]]
//CHECK: subop.nested_map %{{.*}} [@{{.*}}::@coffee_vx,@vars::@who] ([[TUPLE:%.*]], [[NODES:%.*]], [[ANCHOR:%.*]]) {
//CHECK: variant.resolve_local_node [[NODES]] : {{.*}}, [[ANCHOR]]
//CHECK: arith.cmpi sge
//CHECK: subop.lookup {{.*}}[[NODES]] [@idents::@lookup]
//CHECK: subop.gather {{.*}} {graphs_coffee_outgoing${{[0-9]+}} => @edges{{.*}}::@outgoing
//CHECK: gsubop.create_identifier{name = "coffee", id = "http://example.org/eats"}
//CHECK: subop.map {{.*}} computes : [@vars::@food({type = !variant.variant})]
module {
    func.func @subject_anchor() {
        %res = subop.execution_group (){
            %0 = gpm.relalg.named_graph column : @graphs::@ref({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
            %1 = gpm.relalg.triple_pattern %0 @graphs::@ref(?{@vars::@who({type = !gpm.variable_binding})}, id{"http://example.org/drinks"}, ?{@vars::@what({type = !gpm.variable_binding})})
            %2 = gpm.relalg.graph_expansion %1 expand: [@vars::@food({type = !gpm.variable_binding})] pattern: @graphs::@ref(?{@vars::@who}, id{"http://example.org/eats"}, ?{@vars::@food})
            %3 = relalg.materialize %2 [@vars::@who,@vars::@food] => ["who", "food"] : !subop.local_table<[c1 : !db.string, c2 : !db.string], ["who", "food"]>
            subop.execution_group_return %3 : !subop.local_table<[c1 : !db.string, c2 : !db.string], ["who", "food"]>
        } -> !subop.local_table<[c1 : !db.string, c2 : !db.string], ["who", "food"]>
        subop.set_result 0 %res : !subop.local_table<[c1 : !db.string, c2 : !db.string], ["who", "food"]>
        return
    }
}

// -----

// An object anchor is expanded over its incoming edges, a repeated anchor only filters.
//CHECK-LABEL: func.func @object_anchor
//CHECK: variant.resolve_local_node
//CHECK: subop.gather {{.*}} {graphs_coffee_incoming${{[0-9]+}} => @edges{{.*}}::@incoming
//CHECK: subop.map {{.*}} computes : [@vars::@who({type = !variant.variant})]
//CHECK: variant.cmp eq
//CHECK: subop.filter
module {
    func.func @object_anchor() {
        %res = subop.execution_group (){
            %0 = gpm.relalg.named_graph column : @graphs::@ref({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
            %1 = gpm.relalg.triple_pattern %0 @graphs::@ref(?{@vars::@x({type = !gpm.variable_binding})}, ?{@vars::@p({type = !gpm.variable_binding})}, ?{@vars::@food({type = !gpm.variable_binding})})
            %2 = gpm.relalg.graph_expansion %1 expand: [@vars::@who({type = !gpm.variable_binding})] pattern: @graphs::@ref(?{@vars::@who}, ?{@vars::@p}, ?{@vars::@food})
            %3 = relalg.materialize %2 [@vars::@who,@vars::@food] => ["who", "food"] : !subop.local_table<[c1 : !db.string, c2 : !db.string], ["who", "food"]>
            subop.execution_group_return %3 : !subop.local_table<[c1 : !db.string, c2 : !db.string], ["who", "food"]>
        } -> !subop.local_table<[c1 : !db.string, c2 : !db.string], ["who", "food"]>
        subop.set_result 0 %res : !subop.local_table<[c1 : !db.string, c2 : !db.string], ["who", "food"]>
        return
    }
}

// -----

// Nullable anchors (from OPTIONAL) are not supported by the lowering yet.
module {
    func.func @nullable_anchor() {
        %res = subop.execution_group (){
            %0 = gpm.relalg.named_graph column : @graphs::@ref({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
            %1 = gpm.relalg.triple_pattern %0 @graphs::@ref(?{@vars::@person({type = !gpm.variable_binding})}, id{"http://example.org/eats"}, ?{@vars::@food({type = !gpm.variable_binding})})
            %2 = gpm.relalg.named_graph column : @graphs2::@ref({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
            %3 = gpm.relalg.triple_pattern %2 @graphs2::@ref(?{@bindings::@s({type = !gpm.variable_binding})}, id{"http://example.org/cup"}, ?{@vars::@cup({type = !gpm.variable_binding})})
            %4 = relalg.outerjoin %1, %3 (%arg0: !tuples.tuple){
                %6 = tuples.getcol %arg0 @vars::@person : !gpm.variable_binding
                %7 = tuples.getcol %arg0 @bindings::@s : !gpm.variable_binding
                %8 = gpm.bindings_compatible %6 : !gpm.variable_binding, %7 : !gpm.variable_binding
                tuples.return %8 : i1
            } mapping: {@outerjoin::@cup({type = !db.nullable<!gpm.variable_binding>})=[@vars::@cup]}
            // expected-error @+2 {{nullable anchor @outerjoin::@cup is not yet supported by the graph_expansion lowering}}
            // expected-error @+1 {{failed to legalize operation 'gpm.relalg.graph_expansion'}}
            %5 = gpm.relalg.graph_expansion %4 expand: [@vars::@x({type = !gpm.variable_binding})] pattern: @graphs::@ref(?{@outerjoin::@cup}, id{"http://example.org/contains"}, ?{@vars::@x})
            %9 = relalg.materialize %5 [@vars::@person,@vars::@x] => ["person", "x"] : !subop.local_table<[c1 : !db.string, c2 : !db.string], ["person", "x"]>
            subop.execution_group_return %9 : !subop.local_table<[c1 : !db.string, c2 : !db.string], ["person", "x"]>
        } -> !subop.local_table<[c1 : !db.string, c2 : !db.string], ["person", "x"]>
        subop.set_result 0 %res : !subop.local_table<[c1 : !db.string, c2 : !db.string], ["person", "x"]>
        return
    }
}
