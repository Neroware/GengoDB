// RUN: mlir-db-opt %s -split-input-file -verify-diagnostics --to-graph-subop | FileCheck %s

// ?x ex:rel ?y . ?x (ex:rel|ex:q)+ ?z  — semi-naive BFS over subop.loop
//CHECK-LABEL: func.func @plus
//CHECK: gsubop.get_external_graph
//CHECK: subop.materialize
//CHECK: variant.resolve_node
//CHECK: subop.lookup_or_insert
//CHECK: subop.loop
//CHECK: gsubop.scan_graph
//CHECK: subop.scan %arg0
//CHECK: subop.gather {{.*}} {graphs_coffee_outgoing{{.*}}=> @edges{{.*}}::@outgoing
//CHECK: gsubop.scan_edge_set
//CHECK: gsubop.filter_by_identifier {{.*}}[%{{[0-9]+}}, %{{[0-9]+}}]
//CHECK: subop.lookup_or_insert
//CHECK: subop.filter {{.*}} none_true
//CHECK: subop.scatter
//CHECK: subop.loop_continue
//CHECK: subop.create !subop.multimap
//CHECK: subop.insert
//CHECK: subop.lookup {{.*}} !subop.multimap
//CHECK: subop.scan_list
//CHECK: variant.create_node_ref
//CHECK: relalg.materialize
module {
    func.func @plus() {
        %res = subop.execution_group (){
            %0 = gpm.relalg.named_graph column : @graphs::@ref({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
            %1 = gpm.relalg.triple_pattern %0 @graphs::@ref(?{@vars::@x({type = !gpm.variable_binding})}, id{"http://example.org/rel"}, ?{@vars::@y({type = !gpm.variable_binding})})
            %2 = gpm.relalg.path_expansion %1 @graphs::@ref(?{@vars::@x} -> ?{@vars::@z({type = !gpm.variable_binding})}) step: [id{"http://example.org/rel"}, id{"http://example.org/q"}] min: 1 anchor: outgoing
            %t = relalg.materialize %2 [@vars::@x, @vars::@y, @vars::@z] => ["x", "y", "z"] : !subop.local_table<[col1: !db.string, col2: !db.string, col3: !db.string],["x", "y", "z"]>
            subop.execution_group_return %t : !subop.local_table<[col1: !db.string, col2: !db.string, col3: !db.string],["x", "y", "z"]>
        } -> !subop.local_table<[col1: !db.string, col2: !db.string, col3: !db.string],["x", "y", "z"]>
        subop.set_result 0 %res : !subop.local_table<[col1: !db.string, col2: !db.string, col3: !db.string],["x", "y", "z"]>
        return
    }
}

// -----

// both ends bound, backwards from a constant: reachability check on node ids, no new column
//CHECK-LABEL: func.func @bound_target
//CHECK: subop.loop
//CHECK: subop.gather {{.*}} {graphs_coffee_incoming{{.*}}=> @edges{{.*}}::@incoming
//CHECK: subop.loop_continue
//CHECK: subop.scan_list
//CHECK: variant.resolve_node
//CHECK: arith.cmpi eq
//CHECK: subop.filter {{.*}} all_true
//CHECK-NOT: variant.create_node_ref
//CHECK: relalg.materialize
module {
    func.func @bound_target() {
        %res = subop.execution_group (){
            %0 = gpm.relalg.named_graph column : @graphs::@ref({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
            %1 = gpm.relalg.triple_pattern %0 @graphs::@ref(?{@vars::@x({type = !gpm.variable_binding})}, id{"http://example.org/cup"}, ?{@vars::@y({type = !gpm.variable_binding})})
            %2 = gpm.relalg.path_expansion %1 @graphs::@ref(id{"http://example.org/coffee"} -> ?{@vars::@x}) step: [id{"http://example.org/cup"}, id{"http://example.org/contains"}] min: 1 anchor: incoming
            %t = relalg.materialize %2 [@vars::@x, @vars::@y] => ["x", "y"] : !subop.local_table<[col1: !db.string, col2: !db.string],["x", "y"]>
            subop.execution_group_return %t : !subop.local_table<[col1: !db.string, col2: !db.string],["x", "y"]>
        } -> !subop.local_table<[col1: !db.string, col2: !db.string],["x", "y"]>
        subop.set_result 0 %res : !subop.local_table<[col1: !db.string, col2: !db.string],["x", "y"]>
        return
    }
}

// -----

module {
    func.func @nullable_anchor() {
        %res = subop.execution_group (){
            %0 = gpm.relalg.named_graph column : @graphs::@ref({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
            %1 = relalg.const_relation columns : [@c::@x({type = !db.nullable<i32>})] values : [1]
            // expected-error @+2 {{nullable anchors not yet supported}}
            // expected-error @+1 {{failed to legalize operation 'gpm.relalg.path_expansion'}}
            %2 = gpm.relalg.path_expansion %1 @graphs::@ref(?{@c::@x} -> ?{@vars::@z({type = !gpm.variable_binding})}) step: [id{"http://example.org/rel"}] min: 1 anchor: outgoing
            %t = relalg.materialize %2 [@vars::@z] => ["z"] : !subop.local_table<[col1: !db.string],["z"]>
            subop.execution_group_return %t : !subop.local_table<[col1: !db.string],["z"]>
        } -> !subop.local_table<[col1: !db.string],["z"]>
        subop.set_result 0 %res : !subop.local_table<[col1: !db.string],["z"]>
        return
    }
}
