// RUN: mlir-db-opt %s -split-input-file --to-graph-subop | FileCheck %s

// ?x rel+ ?z: seeds = sources of the step edges, closure on subop.loop, both ends wrapped into variants.
//CHECK-LABEL: func.func @unbound_plus
//CHECK-NOT: gpm.relalg.path
//CHECK: gsubop.scan_edge_set
//CHECK: gsubop.filter_by_identifier
//CHECK: subop.lookup_or_insert
//CHECK: subop.loop
//CHECK: gsubop.filter_by_identifier
//CHECK: subop.loop_continue
//CHECK: variant.create_node_ref
//CHECK: variant.create_node_ref
//CHECK: relalg.materialize
module  {
    func.func @unbound_plus() {
        %res = subop.execution_group (){
            %0 = gpm.relalg.named_graph column : @graphs::@ref({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
            %1 = gpm.relalg.path %0 @graphs::@ref(?{@vars::@x({type = !variant.variant})}, ?{@vars::@z({type = !variant.variant})}) step: [id{"http://example.org/rel"}] min: 1
            %res_table = relalg.materialize %1 [@vars::@x, @vars::@z] => ["x", "z"] : !subop.local_table<[col1: !db.string, col2: !db.string],["x", "z"]>
            subop.execution_group_return %res_table : !subop.local_table<[col1: !db.string, col2: !db.string],["x", "z"]>
        } -> !subop.local_table<[col1: !db.string, col2: !db.string],["x", "z"]>
        subop.set_result 0 %res : !subop.local_table<[col1: !db.string, col2: !db.string],["x", "z"]>
        return
    }
}

// -----

// ?x rel* ?z: seeds = nodes(G), the sources and the targets of all edges (no predicate filter)
//CHECK-LABEL: func.func @unbound_star
//CHECK: gsubop.scan_edge_set
//CHECK-NOT: gsubop.filter_by_identifier
//CHECK: gsubop.scan_edge_set
//CHECK-NOT: gsubop.filter_by_identifier
//CHECK: subop.lookup_or_insert
//CHECK-NOT: gsubop.filter_by_identifier
//CHECK: subop.lookup_or_insert
//CHECK-NOT: gsubop.filter_by_identifier
//CHECK: subop.loop
module  {
    func.func @unbound_star() {
        %res = subop.execution_group (){
            %0 = gpm.relalg.named_graph column : @graphs::@ref({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
            %1 = gpm.relalg.path %0 @graphs::@ref(?{@vars::@x({type = !variant.variant})}, ?{@vars::@z({type = !variant.variant})}) step: [id{"http://example.org/rel"}] min: 0
            %res_table = relalg.materialize %1 [@vars::@x, @vars::@z] => ["x", "z"] : !subop.local_table<[col1: !db.string, col2: !db.string],["x", "z"]>
            subop.execution_group_return %res_table : !subop.local_table<[col1: !db.string, col2: !db.string],["x", "z"]>
        } -> !subop.local_table<[col1: !db.string, col2: !db.string],["x", "z"]>
        subop.set_result 0 %res : !subop.local_table<[col1: !db.string, col2: !db.string],["x", "z"]>
        return
    }
}

// -----

// <a> rel* ?z: the constant is the only seed; if it isn't a node of G it still reaches itself (union)
//CHECK-LABEL: func.func @constant_star
//CHECK: gsubop.create_identifier{{.*}}id = "http://example.org/a"
//CHECK: subop.loop
//CHECK: variant.create_node_ref
//CHECK: gsubop.identifier_valid
//CHECK: variant.create_scalar
//CHECK: subop.union
module  {
    func.func @constant_star() {
        %res = subop.execution_group (){
            %0 = gpm.relalg.named_graph column : @graphs::@ref({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
            %1 = gpm.relalg.path %0 @graphs::@ref(id{"http://example.org/a"}, ?{@vars::@z({type = !variant.variant})}) step: [id{"http://example.org/rel"}] min: 0
            %res_table = relalg.materialize %1 [@vars::@z] => ["z"] : !subop.local_table<[col1: !db.string],["z"]>
            subop.execution_group_return %res_table : !subop.local_table<[col1: !db.string],["z"]>
        } -> !subop.local_table<[col1: !db.string],["z"]>
        subop.set_result 0 %res : !subop.local_table<[col1: !db.string],["z"]>
        return
    }
}

// -----

// ?x rel+ ?x: only the pairs (x, x), wrapped once
//CHECK-LABEL: func.func @same_variable
//CHECK: subop.loop
//CHECK: subop.loop_continue
//CHECK: arith.cmpi eq
//CHECK: subop.filter
//CHECK: variant.create_node_ref
//CHECK-NOT: variant.create_node_ref
//CHECK: relalg.materialize
module  {
    func.func @same_variable() {
        %res = subop.execution_group (){
            %0 = gpm.relalg.named_graph column : @graphs::@ref({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
            %1 = gpm.relalg.path %0 @graphs::@ref(?{@vars::@x({type = !variant.variant})}, ?{@vars::@x({type = !variant.variant})}) step: [id{"http://example.org/rel"}] min: 1
            %res_table = relalg.materialize %1 [@vars::@x] => ["x"] : !subop.local_table<[col1: !db.string],["x"]>
            subop.execution_group_return %res_table : !subop.local_table<[col1: !db.string],["x"]>
        } -> !subop.local_table<[col1: !db.string],["x"]>
        subop.set_result 0 %res : !subop.local_table<[col1: !db.string],["x"]>
        return
    }
}
