// RUN: mlir-db-opt %s -split-input-file --to-graph-subop --to-subop | FileCheck %s

// ?who :eats ?what OPTIONAL { ?who :age ?age } -- outer join lowered as graph index nested loop join (R ⟕ 三).
// Expansion, residual predicate and the outer join's no-match marker must all live in the per-probe-tuple
// scope (the nested_map body), otherwise the marker would detect "any probe tuple matched".
// Expected (coffee.ttl): same rows as the hash outer join (eaters without age padded with NULL)
//CHECK-LABEL: func.func @optional_single
//CHECK-NOT: gpm.relalg.triple_pattern
//CHECK: subop.nested_map %{{[0-9]+}} [] (
//CHECK-NEXT: gsubop.scan_graph
//CHECK-NEXT: subop.combine_tuple
//CHECK: @edges{{.*}}::@outgoing
//CHECK: subop.create_simple_state
//CHECK: subop.scatter
//CHECK: subop.scan
//CHECK: subop.filter {{.*}} none_true
//CHECK: db.null
//CHECK: subop.union
//CHECK-NEXT: tuples.return
//CHECK-NEXT: }
//CHECK: subop.materialize
module {
  func.func @optional_single() {
    %0 = subop.execution_group (){
      %1 = gpm.relalg.named_graph column : @graphs_u_1::@ref({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
      %2 = gpm.relalg.triple_pattern %1 @graphs_u_1::@ref(?{@vars::@who({type = !variant.variant})}, id{"http://example.org/eats"}, ?{@vars::@what({type = !variant.variant})})
      %3 = gpm.relalg.named_graph column : @graphs_u_2::@ref({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
      %4 = gpm.relalg.triple_pattern %3 @graphs_u_2::@ref(?{@bindings::@s({type = !variant.variant})}, id{"http://example.org/age"}, ?{@vars::@age({type = !variant.variant})}) {virtual}
      %5 = relalg.outerjoin %2, %4 (%arg0: !tuples.tuple){
        %7 = tuples.getcol %arg0 @vars::@who : !variant.variant
        %8 = tuples.getcol %arg0 @bindings::@s : !variant.variant
        %9 = gpm.bindings_compatible %7 : !variant.variant, %8 : !variant.variant
        tuples.return %9 : i1
      }  mapping: {@outerjoin::@age({type = !db.nullable<!variant.variant>})=[@vars::@age]} attributes {bindingCompatible = [1 : i8], graph = #tuples.columnref<@graphs_u_2::@ref>, impl = "graphIndex", leftHash = [#tuples.columnref<@vars::@who>], nullsEqual = [0 : i8], rightHash = [#tuples.columnref<@bindings::@s>], rows = 1.000000e+00 : f64, useGraphIndex}
      %6 = relalg.materialize %5 [@vars::@who,@outerjoin::@age] => ["who", "age"] : !subop.local_table<[col1 : !db.string, col2 : !db.string], ["who", "age"]>
      subop.execution_group_return %6 : !subop.local_table<[col1 : !db.string, col2 : !db.string], ["who", "age"]>
    } -> !subop.local_table<[col1 : !db.string, col2 : !db.string], ["who", "age"]>
    subop.set_result 0 %0 : !subop.local_table<[col1 : !db.string, col2 : !db.string], ["who", "age"]>
    return
  }
}

