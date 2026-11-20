// RUN: mlir-db-opt %s -split-input-file --to-graph-subop --to-subop | FileCheck %s

// ?p :cup ?c . ?c :contains ?d . ?d :asksForMore ?m -- two stacked graph index joins, both subject anchored.
// The probe side of the outer join is itself the result of a graph index join.
// Expected (coffee.ttl): 1 row (ex:bob, _:cup1, ex:coffee, ex:coffee)
//CHECK-LABEL: func.func @chain
//CHECK-NOT: gpm.relalg.triple_pattern
//CHECK: subop.nested_map
//CHECK: gsubop.scan_graph
//CHECK: @edges{{.*}}::@outgoing
//CHECK: subop.nested_map
//CHECK: gsubop.scan_graph
//CHECK: @edges{{.*}}::@outgoing
//CHECK-NOT: subop.union
//CHECK: subop.materialize
module {
  func.func @chain() {
    %0 = relalg.query (){
      %1 = gpm.relalg.named_graph column : @graphs_u_1::@ref({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
      %2 = gpm.relalg.triple_pattern %1 @graphs_u_1::@ref(?{@vars::@p({type = !variant.variant})}, id{"http://example.org/cup"}, ?{@vars::@c({type = !variant.variant})})
      %3 = gpm.relalg.named_graph column : @graphs_u_2::@ref({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
      %4 = gpm.relalg.triple_pattern %3 @graphs_u_2::@ref(?{@bindings::@s({type = !variant.variant})}, id{"http://example.org/contains"}, ?{@vars::@d({type = !variant.variant})}) {virtual}
      %5 = relalg.join %2, %4 (%arg0: !tuples.tuple){
        %10 = tuples.getcol %arg0 @vars::@c : !variant.variant
        %11 = tuples.getcol %arg0 @bindings::@s : !variant.variant
        %12 = gpm.bindings_compatible %10 : !variant.variant, %11 : !variant.variant
        tuples.return %12 : i1
      } attributes {bindingCompatible = [1 : i8], graph = #tuples.columnref<@graphs_u_2::@ref>, impl = "graphIndex", leftHash = [#tuples.columnref<@vars::@c>], nullsEqual = [0 : i8], rightHash = [#tuples.columnref<@bindings::@s>], useGraphIndex}
      %6 = gpm.relalg.named_graph column : @graphs_u_3::@ref({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
      %7 = gpm.relalg.triple_pattern %6 @graphs_u_3::@ref(?{@bindings_u_1::@s({type = !variant.variant})}, id{"http://example.org/asksForMore"}, ?{@vars::@m({type = !variant.variant})}) {virtual}
      %8 = relalg.join %5, %7 (%arg0: !tuples.tuple){
        %10 = tuples.getcol %arg0 @vars::@d : !variant.variant
        %11 = tuples.getcol %arg0 @bindings_u_1::@s : !variant.variant
        %12 = gpm.bindings_compatible %10 : !variant.variant, %11 : !variant.variant
        tuples.return %12 : i1
      } attributes {bindingCompatible = [1 : i8], graph = #tuples.columnref<@graphs_u_3::@ref>, impl = "graphIndex", leftHash = [#tuples.columnref<@vars::@d>], nullsEqual = [0 : i8], rightHash = [#tuples.columnref<@bindings_u_1::@s>], useGraphIndex}
      %9 = relalg.materialize %8 [@vars::@p,@vars::@c,@vars::@d,@vars::@m] => ["p", "c", "d", "m"] : !subop.local_table<[col1$0 : !db.string, col2$0 : !db.string, col3$0 : !db.string, col4$0 : !db.string], ["p", "c", "d", "m"]>
      relalg.query_return %9 : !subop.local_table<[col1$0 : !db.string, col2$0 : !db.string, col3$0 : !db.string, col4$0 : !db.string], ["p", "c", "d", "m"]>
    } -> !subop.local_table<[col1$0 : !db.string, col2$0 : !db.string, col3$0 : !db.string, col4$0 : !db.string], ["p", "c", "d", "m"]>
    subop.set_result 0 %0 : !subop.local_table<[col1$0 : !db.string, col2$0 : !db.string, col3$0 : !db.string, col4$0 : !db.string], ["p", "c", "d", "m"]>
    return
  }
}

// -----

// ?s :asksForMore ?o . ?o :asksForMore ?s -- two join keys. The subject (key 0) becomes the anchor and is
// bound into the expansion; the object (key 1) stays a residual bindings_compatible in the predicate.
// Expected (coffee.ttl): 1 row (ex:coffee, ex:coffee)
//CHECK-LABEL: func.func @two_keys
//CHECK-NOT: gpm.relalg.triple_pattern
//CHECK: subop.nested_map
//CHECK: gsubop.scan_graph
//CHECK: @edges{{.*}}::@outgoing
//CHECK: subop.filter
//CHECK-NOT: subop.union
//CHECK: subop.materialize
module {
  func.func @two_keys() {
    %0 = relalg.query (){
      %1 = gpm.relalg.named_graph column : @graphs_u_1::@ref({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
      %2 = gpm.relalg.triple_pattern %1 @graphs_u_1::@ref(?{@vars::@s({type = !variant.variant})}, id{"http://example.org/asksForMore"}, ?{@vars::@o({type = !variant.variant})})
      %3 = gpm.relalg.named_graph column : @graphs_u_2::@ref({type = !gpm.graph_ref<"coffee", "file://resources/ttl/coffee/coffee.ttl#rdf">})
      %4 = gpm.relalg.triple_pattern %3 @graphs_u_2::@ref(?{@bindings::@s({type = !variant.variant})}, id{"http://example.org/asksForMore"}, ?{@bindings_u_1::@o({type = !variant.variant})}) {virtual}
      %5 = relalg.join %2, %4 (%arg0: !tuples.tuple){
        %7 = tuples.getcol %arg0 @vars::@o : !variant.variant
        %8 = tuples.getcol %arg0 @bindings::@s : !variant.variant
        %9 = gpm.bindings_compatible %7 : !variant.variant, %8 : !variant.variant
        %10 = tuples.getcol %arg0 @vars::@s : !variant.variant
        %11 = tuples.getcol %arg0 @bindings_u_1::@o : !variant.variant
        %12 = gpm.bindings_compatible %10 : !variant.variant, %11 : !variant.variant
        %13 = db.and %9, %12 : i1, i1
        tuples.return %13 : i1
      } attributes {bindingCompatible = [1 : i8, 1 : i8], graph = #tuples.columnref<@graphs_u_2::@ref>, impl = "graphIndex", leftHash = [#tuples.columnref<@vars::@o>, #tuples.columnref<@vars::@s>], nullsEqual = [0 : i8, 0 : i8], rightHash = [#tuples.columnref<@bindings::@s>, #tuples.columnref<@bindings_u_1::@o>], useGraphIndex}
      %6 = relalg.materialize %5 [@vars::@s,@vars::@o] => ["s", "o"] : !subop.local_table<[col1$0 : !db.string, col2$0 : !db.string], ["s", "o"]>
      relalg.query_return %6 : !subop.local_table<[col1$0 : !db.string, col2$0 : !db.string], ["s", "o"]>
    } -> !subop.local_table<[col1$0 : !db.string, col2$0 : !db.string], ["s", "o"]>
    subop.set_result 0 %0 : !subop.local_table<[col1$0 : !db.string, col2$0 : !db.string], ["s", "o"]>
    return
  }
}
