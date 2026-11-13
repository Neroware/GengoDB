// RUN: mlir-db-opt %s -split-input-file --gpm-optimize-joins | FileCheck %s

// OPTIONAL joins (R ⟕ 三) are lowered as graph index joins: the optional (right) triple becomes virtual.
// A nullable probe key is supported: like every binding compatible join, GINLJ merges an unbound probe value in place
// with the value of the joined triple (the left key column holds the merged binding downstream).
//CHECK-LABEL: func.func @merged_nullable_key
//CHECK: gpm.relalg.triple_pattern {{.*}}"http://example.org/age"{{.*}} {virtual}
//CHECK: relalg.outerjoin
//CHECK: impl = "graphIndex", leftHash = [#tuples.columnref<@vars::@who>]{{.*}}useGraphIndex
//CHECK: gpm.relalg.triple_pattern {{.*}}"http://example.org/age"{{.*}} {virtual}
//CHECK: relalg.outerjoin
//CHECK: impl = "graphIndex", leftHash = [#tuples.columnref<@vars::@who2>, #tuples.columnref<@outerjoin::@age>]{{.*}}useGraphIndex
module {
  func.func @merged_nullable_key() {
    %0 = relalg.query (){
      %1 = gpm.relalg.named_graph column : @graphs_u_1::@ref({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
      %2 = gpm.relalg.triple_pattern %1 @graphs_u_1::@ref(?{@vars::@who({type = !variant.variant})}, id{"http://example.org/eats"}, ?{@vars::@what({type = !variant.variant})})
      %3 = gpm.relalg.named_graph column : @graphs_u_2::@ref({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
      %4 = gpm.relalg.triple_pattern %3 @graphs_u_2::@ref(?{@bindings::@s({type = !variant.variant})}, id{"http://example.org/age"}, ?{@vars::@age({type = !variant.variant})})
      %5 = relalg.outerjoin %2, %4 (%arg0: !tuples.tuple){
        %13 = tuples.getcol %arg0 @vars::@who : !variant.variant
        %14 = tuples.getcol %arg0 @bindings::@s : !variant.variant
        %15 = gpm.bindings_compatible %13 : !variant.variant, %14 : !variant.variant
        tuples.return %15 : i1
      }  mapping: {@outerjoin::@age({type = !db.nullable<!variant.variant>})=[@vars::@age]} attributes {bindingCompatible = [1 : i8], leftHash = [#tuples.columnref<@vars::@who>], nullsEqual = [0 : i8], rightHash = [#tuples.columnref<@bindings::@s>]}
      %6 = gpm.relalg.named_graph column : @graphs_u_3::@ref({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
      %7 = gpm.relalg.triple_pattern %6 @graphs_u_3::@ref(?{@vars::@who2({type = !variant.variant})}, id{"http://example.org/eats"}, ?{@vars::@what2({type = !variant.variant})})
      %8 = relalg.join %5, %7 (%arg0: !tuples.tuple){
        tuples.return
      }
      %9 = gpm.relalg.named_graph column : @graphs_u_4::@ref({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
      %10 = gpm.relalg.triple_pattern %9 @graphs_u_4::@ref(?{@bindings_u_1::@s({type = !variant.variant})}, id{"http://example.org/age"}, ?{@bindings_u_2::@o({type = !variant.variant})})
      %11 = relalg.outerjoin %8, %10 (%arg0: !tuples.tuple){
        %13 = tuples.getcol %arg0 @vars::@who2 : !variant.variant
        %14 = tuples.getcol %arg0 @bindings_u_1::@s : !variant.variant
        %15 = gpm.bindings_compatible %13 : !variant.variant, %14 : !variant.variant
        %16 = tuples.getcol %arg0 @outerjoin::@age : !db.nullable<!variant.variant>
        %17 = tuples.getcol %arg0 @bindings_u_2::@o : !variant.variant
        %18 = gpm.bindings_compatible %16 : !db.nullable<!variant.variant>, %17 : !variant.variant
        %19 = db.and %15, %18 : i1, i1
        tuples.return %19 : i1
      }  mapping: {@outerjoin_u_1::@o({type = !db.nullable<!variant.variant>})=[@bindings_u_2::@o]} attributes {bindingCompatible = [1 : i8, 1 : i8], leftHash = [#tuples.columnref<@vars::@who2>, #tuples.columnref<@outerjoin::@age>], nullsEqual = [0 : i8, 0 : i8], rightHash = [#tuples.columnref<@bindings_u_1::@s>, #tuples.columnref<@bindings_u_2::@o>]}
      %12 = relalg.materialize %11 [@vars::@who,@vars::@what,@outerjoin::@age,@vars::@who2,@vars::@what2] => ["who", "what", "age", "who2", "what2"] : !subop.local_table<[col1$0 : !db.string, col2$0 : !db.string, col3$0 : !db.string, col4$0 : !db.string, col5$0 : !db.string], ["who", "what", "age", "who2", "what2"]>
      relalg.query_return %12 : !subop.local_table<[col1$0 : !db.string, col2$0 : !db.string, col3$0 : !db.string, col4$0 : !db.string, col5$0 : !db.string], ["who", "what", "age", "who2", "what2"]>
    } -> !subop.local_table<[col1$0 : !db.string, col2$0 : !db.string, col3$0 : !db.string, col4$0 : !db.string, col5$0 : !db.string], ["who", "what", "age", "who2", "what2"]>
    subop.set_result 0 %0 : !subop.local_table<[col1$0 : !db.string, col2$0 : !db.string, col3$0 : !db.string, col4$0 : !db.string, col5$0 : !db.string], ["who", "what", "age", "who2", "what2"]>
    return
  }
}


// -----

// The only anchor candidate (?cup) is unbound for probe tuples without a cup. It is still a graph index join
// (binding compatible): bound probe tuples use the index, unbound ones join with the full expansion of the triple.
// The join merges the unbound ?cup in place with the joined triple's value.
//CHECK-LABEL: func.func @nullable_anchor
//CHECK: relalg.outerjoin
//CHECK: impl = "graphIndex"
//CHECK: relalg.outerjoin
//CHECK: impl = "graphIndex", leftHash = [#tuples.columnref<@outerjoin::@cup>]{{.*}}useGraphIndex
//CHECK: relalg.materialize
module {
  func.func @nullable_anchor() {
    %0 = subop.execution_group (){
      %1 = gpm.relalg.named_graph column : @graphs_u_1::@ref({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
      %2 = gpm.relalg.triple_pattern %1 @graphs_u_1::@ref(?{@vars::@person({type = !variant.variant})}, id{"http://example.org/eats"}, ?{@vars::@food({type = !variant.variant})})
      %3 = gpm.relalg.named_graph column : @graphs_u_2::@ref({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
      %4 = gpm.relalg.triple_pattern %3 @graphs_u_2::@ref(?{@bindings::@s({type = !variant.variant})}, id{"http://example.org/cup"}, ?{@vars::@cup({type = !variant.variant})})
      %5 = relalg.outerjoin %2, %4 (%arg0: !tuples.tuple){
        %10 = tuples.getcol %arg0 @vars::@person : !variant.variant
        %11 = tuples.getcol %arg0 @bindings::@s : !variant.variant
        %12 = gpm.bindings_compatible %10 : !variant.variant, %11 : !variant.variant
        tuples.return %12 : i1
      }  mapping: {@outerjoin::@cup({type = !db.nullable<!variant.variant>})=[@vars::@cup]} attributes {bindingCompatible = [1 : i8], leftHash = [#tuples.columnref<@vars::@person>], nullsEqual = [0 : i8], rightHash = [#tuples.columnref<@bindings::@s>]}
      %6 = gpm.relalg.named_graph column : @graphs_u_3::@ref({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
      %7 = gpm.relalg.triple_pattern %6 @graphs_u_3::@ref(?{@bindings_u_1::@s({type = !variant.variant})}, id{"http://example.org/drinks"}, ?{@vars::@drink({type = !variant.variant})})
      %8 = relalg.outerjoin %5, %7 (%arg0: !tuples.tuple){
        %10 = tuples.getcol %arg0 @outerjoin::@cup : !db.nullable<!variant.variant>
        %11 = tuples.getcol %arg0 @bindings_u_1::@s : !variant.variant
        %12 = gpm.bindings_compatible %10 : !db.nullable<!variant.variant>, %11 : !variant.variant
        tuples.return %12 : i1
      }  mapping: {@outerjoin_u_1::@drink({type = !db.nullable<!variant.variant>})=[@vars::@drink], @outerjoin_u_1::@s({type = !db.nullable<!variant.variant>})=[@bindings_u_1::@s]} attributes {bindingCompatible = [1 : i8], leftHash = [#tuples.columnref<@outerjoin::@cup>], nullsEqual = [0 : i8], rightHash = [#tuples.columnref<@bindings_u_1::@s>]}
      %9 = relalg.materialize %8 [@vars::@person,@vars::@food,@outerjoin::@cup,@outerjoin_u_1::@drink] => ["person", "food", "cup", "drink"] : !subop.local_table<[col1 : !db.string, col2 : !db.string, col3 : !db.string, col4 : !db.string], ["person", "food", "cup", "drink"]>
      subop.execution_group_return %9 : !subop.local_table<[col1 : !db.string, col2 : !db.string, col3 : !db.string, col4 : !db.string], ["person", "food", "cup", "drink"]>
    } -> !subop.local_table<[col1 : !db.string, col2 : !db.string, col3 : !db.string, col4 : !db.string], ["person", "food", "cup", "drink"]>
    subop.set_result 0 %0 : !subop.local_table<[col1 : !db.string, col2 : !db.string, col3 : !db.string, col4 : !db.string], ["person", "food", "cup", "drink"]>
    return
  }
}


// -----

// Inner join on a nullable probe key: also a graph index join with a nullable anchor. Unbound probe
// tuples join with the full expansion and ?cup is filled in with the joined triple's value.
//CHECK-LABEL: func.func @inner_nullable_anchor
//CHECK: relalg.outerjoin
//CHECK: impl = "graphIndex"
//CHECK: relalg.join
//CHECK: impl = "graphIndex", leftHash = [#tuples.columnref<@outerjoin::@cup>]{{.*}}useGraphIndex
//CHECK: relalg.materialize
module {
  func.func @inner_nullable_anchor() {
    %0 = subop.execution_group (){
      %1 = gpm.relalg.named_graph column : @graphs_u_1::@ref({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
      %2 = gpm.relalg.triple_pattern %1 @graphs_u_1::@ref(?{@vars::@person({type = !variant.variant})}, id{"http://example.org/eats"}, ?{@vars::@food({type = !variant.variant})})
      %3 = gpm.relalg.named_graph column : @graphs_u_2::@ref({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
      %4 = gpm.relalg.triple_pattern %3 @graphs_u_2::@ref(?{@bindings::@s({type = !variant.variant})}, id{"http://example.org/cup"}, ?{@vars::@cup({type = !variant.variant})})
      %5 = relalg.outerjoin %2, %4 (%arg0: !tuples.tuple){
        %10 = tuples.getcol %arg0 @vars::@person : !variant.variant
        %11 = tuples.getcol %arg0 @bindings::@s : !variant.variant
        %12 = gpm.bindings_compatible %10 : !variant.variant, %11 : !variant.variant
        tuples.return %12 : i1
      }  mapping: {@outerjoin::@cup({type = !db.nullable<!variant.variant>})=[@vars::@cup]} attributes {bindingCompatible = [1 : i8], leftHash = [#tuples.columnref<@vars::@person>], nullsEqual = [0 : i8], rightHash = [#tuples.columnref<@bindings::@s>]}
      %6 = gpm.relalg.named_graph column : @graphs_u_3::@ref({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
      %7 = gpm.relalg.triple_pattern %6 @graphs_u_3::@ref(?{@bindings_u_1::@s({type = !variant.variant})}, id{"http://example.org/drinks"}, ?{@vars::@drink({type = !variant.variant})})
      %8 = relalg.join %5, %7 (%arg0: !tuples.tuple){
        %10 = tuples.getcol %arg0 @outerjoin::@cup : !db.nullable<!variant.variant>
        %11 = tuples.getcol %arg0 @bindings_u_1::@s : !variant.variant
        %12 = gpm.bindings_compatible %10 : !db.nullable<!variant.variant>, %11 : !variant.variant
        tuples.return %12 : i1
      } attributes {bindingCompatible = [1 : i8], leftHash = [#tuples.columnref<@outerjoin::@cup>], nullsEqual = [0 : i8], rightHash = [#tuples.columnref<@bindings_u_1::@s>]}
      %9 = relalg.materialize %8 [@vars::@person,@vars::@food,@outerjoin::@cup,@vars::@drink] => ["person", "food", "cup", "drink"] : !subop.local_table<[col1 : !db.string, col2 : !db.string, col3 : !db.string, col4 : !db.string], ["person", "food", "cup", "drink"]>
      subop.execution_group_return %9 : !subop.local_table<[col1 : !db.string, col2 : !db.string, col3 : !db.string, col4 : !db.string], ["person", "food", "cup", "drink"]>
    } -> !subop.local_table<[col1 : !db.string, col2 : !db.string, col3 : !db.string, col4 : !db.string], ["person", "food", "cup", "drink"]>
    subop.set_result 0 %0 : !subop.local_table<[col1 : !db.string, col2 : !db.string, col3 : !db.string, col4 : !db.string], ["person", "food", "cup", "drink"]>
    return
  }
}
