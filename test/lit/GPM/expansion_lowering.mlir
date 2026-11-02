// RUN: mlir-db-opt %s -split-input-file --to-graph-subop | FileCheck %s

// ?p :cup ?c, then node_expansion(?c -> ?e, outgoing) and edge_expansion(?e -> ?x, to_node).
// The expansions reuse the graph columns of their input; the edge becomes a variant and is resolved back to the graph's edge.
//CHECK-LABEL: func.func @two_step
//CHECK-NOT: gpm.relalg.node_expansion
//CHECK-NOT: gpm.relalg.edge_expansion
//CHECK: gsubop.filter_by_identifier
//CHECK: subop.nested_map
//CHECK: variant.resolve_node
//CHECK: subop.lookup
//CHECK: @edges{{.*}}::@outgoing
//CHECK: gsubop.scan_edge_set
//CHECK: variant.create_edge_ref
//CHECK: variant.resolve_edge
//CHECK: subop.lookup
//CHECK: subop.gather
//CHECK: variant.create_node_ref
//CHECK: relalg.materialize
module  {
    func.func @two_step() {
        %res = subop.execution_group (){
            %0 = gpm.relalg.named_graph column : @graphs::@ref({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
            %1 = gpm.relalg.triple_pattern %0 @graphs::@ref(?{@vars::@p({type = !gpm.variable_binding})}, id{"http://example.org/cup"}, ?{@vars::@c({type = !gpm.variable_binding})})
            %2 = gpm.relalg.node_expansion %1 @graphs::@ref(?{@vars::@c} -> ?{@edges::@e({type = !gpm.variable_binding})}) anchor: outgoing
            %3 = gpm.relalg.edge_expansion %2 @graphs::@ref(?{@edges::@e} -> ?{@vars::@x({type = !gpm.variable_binding})}) anchor: to_node
            %res_table = relalg.materialize %3 [@vars::@p, @vars::@c, @edges::@e, @vars::@x] => ["p", "c", "e", "x"] : !subop.local_table<[col1: !db.string, col2: !db.string, col3: !db.string, col4: !db.string],["p", "c", "e", "x"]>
            subop.execution_group_return %res_table : !subop.local_table<[col1: !db.string, col2: !db.string, col3: !db.string, col4: !db.string],["p", "c", "e", "x"]>
        } -> !subop.local_table<[col1: !db.string, col2: !db.string, col3: !db.string, col4: !db.string],["p", "c", "e", "x"]>
        subop.set_result 0 %res : !subop.local_table<[col1: !db.string, col2: !db.string, col3: !db.string, col4: !db.string],["p", "c", "e", "x"]>
        return
    }
}

// -----

// edge_expansion(:cup -> ?x, to_node): scan of all edges, filtered by the predicate.
//CHECK-LABEL: func.func @label_scan
//CHECK-NOT: gpm.relalg.edge_expansion
//CHECK: gsubop.scan_edge_set
//CHECK: gsubop.filter_by_identifier
//CHECK: subop.gather
//CHECK: variant.create_node_ref
module  {
    func.func @label_scan() {
        %res = subop.execution_group (){
            %0 = gpm.relalg.named_graph column : @graphs::@ref({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
            %out = gpm.relalg.edge_expansion %0 @graphs::@ref(id{"http://example.org/cup"} -> ?{@vars::@x({type = !gpm.variable_binding})}) anchor: to_node
            %res_table = relalg.materialize %out [@vars::@x] => ["x"] : !subop.local_table<[col1: !db.string],["x"]>
            subop.execution_group_return %res_table : !subop.local_table<[col1: !db.string],["x"]>
        } -> !subop.local_table<[col1: !db.string],["x"]>
        subop.set_result 0 %res : !subop.local_table<[col1: !db.string],["x"]>
        return
    }
}
