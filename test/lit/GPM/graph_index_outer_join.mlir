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


// -----

// ?person :eats ?food OPTIONAL { ?person :cup ?cup } OPTIONAL { ?cup :drinks ?drink } -- the second GINLJ has a
// nullable anchor (?cup). Binding compatible: bound probe tuples expand ?cup via the index, unbound ones join with
// the full expansion of the triple, which is materialized once outside and scanned only for unbound probe tuples.
// Afterwards, the unbound ?cup is filled in with the value of the joined triple (cup := isnull(cup) ? s : cup).
// Expected (coffee.ttl): same rows as the hash outer join
//CHECK-LABEL: func.func @nullable_anchor
//CHECK-NOT: gpm.relalg.triple_pattern
//CHECK: [[BUF:%[0-9]+]] = subop.create !subop.buffer
//CHECK: subop.materialize {{.*}}, [[BUF]]
//CHECK: subop.nested_map %{{[0-9]+}} [] (
//CHECK-NEXT: gsubop.scan_graph
//CHECK-NEXT: subop.combine_tuple
//CHECK: db.isnull
//CHECK: subop.filter {{.*}} none_true [@graphindex{{.*}}::@unbound]
//CHECK: [[UNBOUND:%[0-9]+]] = subop.filter {{.*}} all_true [@graphindex{{.*}}::@unbound]
//CHECK: @edges{{.*}}::@outgoing
//CHECK: subop.nested_map [[UNBOUND]] [] (
//CHECK-NEXT: subop.scan [[BUF]]
//CHECK-NEXT: subop.combine_tuple
//CHECK-NEXT: tuples.return
//CHECK: subop.union
//CHECK-NEXT: subop.map {{.*}} computes : [@outerjoin::@cup({{.*}})] input : [@outerjoin::@cup,
//CHECK: subop.create_simple_state
//CHECK: subop.filter {{.*}} none_true [@marker
//CHECK: subop.union
//CHECK-NEXT: tuples.return
//CHECK-NEXT: }
//CHECK: subop.materialize {{.*}}@merge::@cup
module {
  func.func @nullable_anchor() {
    %0 = subop.execution_group (){
      %1 = gpm.relalg.named_graph column : @graphs_u_1::@ref({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
      %2 = gpm.relalg.triple_pattern %1 @graphs_u_1::@ref(?{@vars::@person({type = !variant.variant})}, id{"http://example.org/eats"}, ?{@vars::@food({type = !variant.variant})})
      %3 = gpm.relalg.named_graph column : @graphs_u_2::@ref({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
      %4 = gpm.relalg.triple_pattern %3 @graphs_u_2::@ref(?{@bindings::@s({type = !variant.variant})}, id{"http://example.org/cup"}, ?{@vars::@cup({type = !variant.variant})}) {virtual}
      %5 = relalg.outerjoin %2, %4 (%arg0: !tuples.tuple){
        %10 = tuples.getcol %arg0 @vars::@person : !variant.variant
        %11 = tuples.getcol %arg0 @bindings::@s : !variant.variant
        %12 = gpm.bindings_compatible %10 : !variant.variant, %11 : !variant.variant
        tuples.return %12 : i1
      }  mapping: {@outerjoin::@cup({type = !db.nullable<!variant.variant>})=[@vars::@cup]} attributes {bindingCompatible = [1 : i8], graph = #tuples.columnref<@graphs_u_2::@ref>, impl = "graphIndex", leftHash = [#tuples.columnref<@vars::@person>], nullsEqual = [0 : i8], rightHash = [#tuples.columnref<@bindings::@s>], useGraphIndex}
      %6 = gpm.relalg.named_graph column : @graphs_u_3::@ref({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
      %7 = gpm.relalg.triple_pattern %6 @graphs_u_3::@ref(?{@bindings_u_1::@s({type = !variant.variant})}, id{"http://example.org/drinks"}, ?{@vars::@drink({type = !variant.variant})}) {virtual}
      %8 = relalg.outerjoin %5, %7 (%arg0: !tuples.tuple){
        %10 = tuples.getcol %arg0 @outerjoin::@cup : !db.nullable<!variant.variant>
        %11 = tuples.getcol %arg0 @bindings_u_1::@s : !variant.variant
        %12 = gpm.bindings_compatible %10 : !db.nullable<!variant.variant>, %11 : !variant.variant
        tuples.return %12 : i1
      }  mapping: {@outerjoin_u_1::@drink({type = !db.nullable<!variant.variant>})=[@vars::@drink], @outerjoin_u_1::@s({type = !db.nullable<!variant.variant>})=[@bindings_u_1::@s]} attributes {bindingCompatible = [1 : i8], gpm.merge = [#tuples.columndef<@merge::@cup,!db.nullable<!variant.variant>,[#tuples.columnref<@outerjoin::@cup>, #tuples.columnref<@outerjoin_u_1::@s>]>], graph = #tuples.columnref<@graphs_u_3::@ref>, impl = "graphIndex", leftHash = [#tuples.columnref<@outerjoin::@cup>], nullsEqual = [0 : i8], rightHash = [#tuples.columnref<@bindings_u_1::@s>], useGraphIndex}
      %9 = relalg.materialize %8 [@vars::@person,@vars::@food,@merge::@cup,@outerjoin_u_1::@drink] => ["person", "food", "cup", "drink"] : !subop.local_table<[col1 : !db.string, col2 : !db.string, col3 : !db.string, col4 : !db.string], ["person", "food", "cup", "drink"]>
      subop.execution_group_return %9 : !subop.local_table<[col1 : !db.string, col2 : !db.string, col3 : !db.string, col4 : !db.string], ["person", "food", "cup", "drink"]>
    } -> !subop.local_table<[col1 : !db.string, col2 : !db.string, col3 : !db.string, col4 : !db.string], ["person", "food", "cup", "drink"]>
    subop.set_result 0 %0 : !subop.local_table<[col1 : !db.string, col2 : !db.string, col3 : !db.string, col4 : !db.string], ["person", "food", "cup", "drink"]>
    return
  }
}


// -----

// ?person :eats ?food OPTIONAL { ?person :cup ?cup } { ?cup :drinks ?drink } -- inner join with a nullable anchor (?cup).
// Same structure as the outer join (buffer for unbound probe tuples, fill-in of ?cup), the join predicate is a filter.
// Expected (coffee.ttl): same rows as the hash join, ?cup taken from the triple for eaters without a cup
//CHECK-LABEL: func.func @inner_nullable_anchor
//CHECK-NOT: gpm.relalg.triple_pattern
//CHECK: [[BUF:%[0-9]+]] = subop.create !subop.buffer
//CHECK: subop.materialize {{.*}}, [[BUF]]
//CHECK: subop.nested_map %{{[0-9]+}} [] (
//CHECK: subop.filter {{.*}} none_true [@graphindex{{.*}}::@unbound]
//CHECK: [[UNBOUND:%[0-9]+]] = subop.filter {{.*}} all_true [@graphindex{{.*}}::@unbound]
//CHECK: subop.nested_map [[UNBOUND]] [] (
//CHECK-NEXT: subop.scan [[BUF]]
//CHECK: subop.union
//CHECK-NEXT: subop.map {{.*}} computes : [@outerjoin::@cup({{.*}})] input : [@outerjoin::@cup,
//CHECK: subop.filter
//CHECK-NOT: subop.create_simple_state
//CHECK: tuples.return
//CHECK: subop.materialize {{.*}}@outerjoin::@cup
module {
  func.func @inner_nullable_anchor() {
    %0 = subop.execution_group (){
      %1 = gpm.relalg.named_graph column : @graphs_u_1::@ref({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
      %2 = gpm.relalg.triple_pattern %1 @graphs_u_1::@ref(?{@vars::@person({type = !variant.variant})}, id{"http://example.org/eats"}, ?{@vars::@food({type = !variant.variant})})
      %3 = gpm.relalg.named_graph column : @graphs_u_2::@ref({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
      %4 = gpm.relalg.triple_pattern %3 @graphs_u_2::@ref(?{@bindings::@s({type = !variant.variant})}, id{"http://example.org/cup"}, ?{@vars::@cup({type = !variant.variant})}) {virtual}
      %5 = relalg.outerjoin %2, %4 (%arg0: !tuples.tuple){
        %10 = tuples.getcol %arg0 @vars::@person : !variant.variant
        %11 = tuples.getcol %arg0 @bindings::@s : !variant.variant
        %12 = gpm.bindings_compatible %10 : !variant.variant, %11 : !variant.variant
        tuples.return %12 : i1
      }  mapping: {@outerjoin::@cup({type = !db.nullable<!variant.variant>})=[@vars::@cup]} attributes {bindingCompatible = [1 : i8], graph = #tuples.columnref<@graphs_u_2::@ref>, impl = "graphIndex", leftHash = [#tuples.columnref<@vars::@person>], nullsEqual = [0 : i8], rightHash = [#tuples.columnref<@bindings::@s>], useGraphIndex}
      %6 = gpm.relalg.named_graph column : @graphs_u_3::@ref({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
      %7 = gpm.relalg.triple_pattern %6 @graphs_u_3::@ref(?{@bindings_u_1::@s({type = !variant.variant})}, id{"http://example.org/drinks"}, ?{@vars::@drink({type = !variant.variant})}) {virtual}
      %8 = relalg.join %5, %7 (%arg0: !tuples.tuple){
        %10 = tuples.getcol %arg0 @outerjoin::@cup : !db.nullable<!variant.variant>
        %11 = tuples.getcol %arg0 @bindings_u_1::@s : !variant.variant
        %12 = gpm.bindings_compatible %10 : !db.nullable<!variant.variant>, %11 : !variant.variant
        tuples.return %12 : i1
      } attributes {bindingCompatible = [1 : i8], graph = #tuples.columnref<@graphs_u_3::@ref>, impl = "graphIndex", leftHash = [#tuples.columnref<@outerjoin::@cup>], nullsEqual = [0 : i8], rightHash = [#tuples.columnref<@bindings_u_1::@s>], useGraphIndex}
      %9 = relalg.materialize %8 [@vars::@person,@vars::@food,@outerjoin::@cup,@vars::@drink] => ["person", "food", "cup", "drink"] : !subop.local_table<[col1 : !db.string, col2 : !db.string, col3 : !db.string, col4 : !db.string], ["person", "food", "cup", "drink"]>
      subop.execution_group_return %9 : !subop.local_table<[col1 : !db.string, col2 : !db.string, col3 : !db.string, col4 : !db.string], ["person", "food", "cup", "drink"]>
    } -> !subop.local_table<[col1 : !db.string, col2 : !db.string, col3 : !db.string, col4 : !db.string], ["person", "food", "cup", "drink"]>
    subop.set_result 0 %0 : !subop.local_table<[col1 : !db.string, col2 : !db.string, col3 : !db.string, col4 : !db.string], ["person", "food", "cup", "drink"]>
    return
  }
}

